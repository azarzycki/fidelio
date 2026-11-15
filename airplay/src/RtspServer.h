#pragma once
// RAOP RTSP server: accepts connections on the RTSP port and runs one handler thread per
// connection. One audio session at a time; a second ANNOUNCE while busy gets 453.
#include <netinet/in.h>
#include <stdint.h>

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "RaopCrypto.h"
#include "RaopSession.h"

class AudioOutput;
class StatusFile;
class SocketSink;

struct RtspRequest {
  std::string method, uri, version;
  std::map<std::string, std::string> headers;  // lower-case keys
  std::vector<uint8_t> body;
  std::string header(const char* name) const;
};

class RtspServer {
 public:
  // B1.12: what to do with the AirPlay sender when audiod hands the output to another source.
  enum PreemptPolicy { PRE_PAUSE_THEN_DISCONNECT, PRE_PAUSE_ONLY, PRE_DISCONNECT, PRE_NONE };
  struct PreemptOptions { PreemptPolicy policy = PRE_PAUSE_THEN_DISCONNECT; unsigned verifyMs = 3000; unsigned cooldownMs = 10000; };

  RtspServer(uint16_t port, const std::string& name, const uint8_t hwaddr[6], AudioOutput* out, StatusFile* status, int latencyMs);
  void setPreemptOptions(const PreemptOptions& o) { preempt = o; }
  // Called (from the SocketSink thread) when audiod pre-empts AirPlay. Non-blocking: the policy runs
  // on its own thread so the takeover and the new source are never delayed.
  void onPreempted(const std::string& by);
  static void preemptTrampoline(void* self, const char* by) { static_cast<RtspServer*>(self)->onPreempted(by ? by : ""); }
  // B1.16: audiod routed a transport verb to AirPlay -> DACP it to the sender (background, non-blocking).
  void onControl(uint8_t verb);
  static void controlTrampoline(void* self, uint8_t verb) { static_cast<RtspServer*>(self)->onControl(verb); }
  // B1.13: the audiod sink, so AirPlay volume can drive the hardware (absolute mode). The server
  // learns the mode and the current hardware level from the sink's WELCOME/HWVOLUME callback.
  void setSink(::SocketSink* s) { sink = s; }
  void onHardwareVolume(bool absolute, uint16_t slider);  // from the sink (audiod reported the hardware level)
  static void hwVolumeTrampoline(void* self, bool absolute, uint16_t slider) { static_cast<RtspServer*>(self)->onHardwareVolume(absolute, slider); }
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
  void runPreemptPolicy(std::string by);   // B1.12, background thread
  void forceDisconnect(const char* why);   // tear down the session and shut the owner socket; start the cool-down
  void applyAirplayVolume(float db);       // B1.13: route a SET_PARAMETER volume (absolute vs software)
  void pushDeviceVolumeToSender();         // B1.13: DACP setproperty device-volume = current hardware level

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
  // B1.12: remote-control headers from the sender (under sessionMutex), the cool-down, and a guard
  // so only one preempt policy runs at a time.
  PreemptOptions preempt;
  std::string dacpId, dacpActiveRemote, sessionPeerIp;
  std::map<std::string, uint64_t> cooldownUntil;  // peer IP -> monotonic ms until which a new session is refused
  std::atomic<bool> preemptBusy{false};
  // B1.13: AirPlay absolute volume. The mode and the current hardware level come from the audiod
  // sink's WELCOME/HWVOLUME; the opening volume of a session is remembered but not applied.
  ::SocketSink* sink = nullptr;
  bool airplayAbsolute = false;   // audiod runs AirPlay in absolute mode (from WELCOME)
  bool hwKnown = false;
  float hwLevel = 0.0f;           // 0..1, the device output level audiod reported
  bool haveOpeningVol = false;    // the first volume of the current session has been seen
  float openingDb = 0.0f;
};
