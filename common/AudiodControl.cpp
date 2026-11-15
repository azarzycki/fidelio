#include "AudiodControl.h"

#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#include "AudiodFraming.h"
#include "audiod/proto.h"

namespace audiod {

bool sendControl(const std::string& sockPath, uint8_t verb, int timeoutMs) {
  int s = socket(AF_UNIX, SOCK_STREAM, 0);
  if (s < 0) return false;
  struct timeval to = { timeoutMs / 1000, (timeoutMs % 1000) * 1000 };
  setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &to, sizeof to);
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof to);
  sockaddr_un a;
  memset(&a, 0, sizeof a);
  a.sun_family = AF_UNIX;
  strncpy(a.sun_path, sockPath.c_str(), sizeof a.sun_path - 1);
  if (connect(s, (sockaddr*)&a, sizeof a) != 0) { close(s); return false; }
  // HELLO as the non-producing controller "control" (v2); audiod does not add it to the arbiter.
  AudiodHello h = audiod::makeHello("control", 0, AUDIOD_PROTO_VERSION_2);
  bool ok = audiod::writeFrame(s, AUDIOD_MSG_HELLO, &h, sizeof h, timeoutMs);
  if (ok) {
    AudiodControl c; c.verb = verb;
    ok = audiod::writeFrame(s, AUDIOD_MSG_CONTROL, &c, sizeof c, timeoutMs);
  }
  // Give audiod a moment to read before we close, so the frames are not discarded on RST.
  if (ok) { char buf[8]; recv(s, buf, sizeof buf, 0); }  // returns on timeout or EOF; we ignore it
  close(s);
  return ok;
}

}  // namespace audiod

extern "C" int audiod_send_control(const char* sockPath, int verb, int timeoutMs) {
  if (!sockPath || verb < 1 || verb > 255) return 0;
  return audiod::sendControl(sockPath, (uint8_t)verb, timeoutMs > 0 ? timeoutMs : 300) ? 1 : 0;
}
