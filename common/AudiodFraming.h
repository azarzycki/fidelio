#pragma once
// Framing for the fidelio-audiod wire protocol (audiod/proto.h): an incremental parser used by
// the server for every client connection, and a writer used by producers (SocketSink).
// Both sides share this file so the ABI is implemented exactly once.
#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

#include "audiod/proto.h"

namespace audiod {

struct Message {
  uint32_t type = 0;
  std::vector<uint8_t> payload;
};

// Feed bytes as they arrive; pop complete messages. A header whose len exceeds
// AUDIOD_MAX_PAYLOAD, or an unknown type, poisons the parser (error() != "") and the
// connection must be dropped: nothing after it is trusted.
class FrameParser {
 public:
  void feed(const uint8_t* data, size_t len);
  bool next(Message& out);            // true if a complete message was popped
  const std::string& error() const { return err; }
  size_t pending() const { return buf.size() - pos; }

 private:
  std::vector<uint8_t> buf;
  size_t pos = 0;
  std::string err;
};

// Writes header + payload to fd (blocking, retrying on EINTR/partial writes).
// Returns false on any error (EPIPE, ECONNRESET, ...); the caller reconnects.
bool writeFrame(int fd, uint32_t type, const void* payload, size_t len);

// HELLO helpers.
AudiodHello makeHello(const std::string& name, uint8_t priority = 0, uint8_t version = AUDIOD_PROTO_VERSION);
bool parseHello(const Message& m, AudiodHello& out, std::string& why);  // validates size/version (1 or 2)/format
// VOLUME in either form: 4 bytes (gain only) or 8 bytes (gain + raw slider + flags). Clamps the gain
// to 0..1 (NaN -> 0); slider/flags are 0 for the 4-byte form. False on a bad size.
bool parseVolume(const Message& m, float& linear, uint16_t& slider, uint16_t& flags);
std::string helloName(const AudiodHello& h);                            // nul-terminated copy of name[]

}  // namespace audiod
