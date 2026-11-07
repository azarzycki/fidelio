#include "AudiodFraming.h"

#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

namespace audiod {

static bool knownType(uint32_t t) {
  return t == AUDIOD_MSG_HELLO || t == AUDIOD_MSG_AUDIO || t == AUDIOD_MSG_FLUSH || t == AUDIOD_MSG_STOP || t == AUDIOD_MSG_META;
}

void FrameParser::feed(const uint8_t* data, size_t len) {
  if (!err.empty()) return;
  if (pos > 0 && pos >= buf.size() / 2) {  // compact occasionally
    buf.erase(buf.begin(), buf.begin() + (long)pos);
    pos = 0;
  }
  buf.insert(buf.end(), data, data + len);
}

bool FrameParser::next(Message& out) {
  if (!err.empty()) return false;
  if (buf.size() - pos < sizeof(AudiodMsgHeader)) return false;
  AudiodMsgHeader h;
  memcpy(&h, buf.data() + pos, sizeof h);  // little-endian on every target we have
  if (!knownType(h.type)) { err = "unknown message type " + std::to_string(h.type); return false; }
  if (h.len > AUDIOD_MAX_PAYLOAD) { err = "payload " + std::to_string(h.len) + " exceeds AUDIOD_MAX_PAYLOAD"; return false; }
  if (buf.size() - pos < sizeof h + h.len) return false;
  out.type = h.type;
  out.payload.assign(buf.begin() + (long)(pos + sizeof h), buf.begin() + (long)(pos + sizeof h + h.len));
  pos += sizeof h + h.len;
  if (pos == buf.size()) { buf.clear(); pos = 0; }
  return true;
}

bool writeFrame(int fd, uint32_t type, const void* payload, size_t len) {
  if (len > AUDIOD_MAX_PAYLOAD) return false;
  AudiodMsgHeader h = { type, (uint32_t)len };
  struct iovec iov[2] = { { &h, sizeof h }, { const_cast<void*>(payload), len } };
  int n = len ? 2 : 1;
  size_t total = sizeof h + len, done = 0;
  while (done < total) {
    // skip fully written iovecs
    struct iovec cur[2];
    int c = 0;
    size_t skip = done;
    for (int i = 0; i < n; i++) {
      if (skip >= iov[i].iov_len) { skip -= iov[i].iov_len; continue; }
      cur[c].iov_base = (uint8_t*)iov[i].iov_base + skip;
      cur[c].iov_len = iov[i].iov_len - skip;
      skip = 0;
      c++;
    }
    ssize_t w = writev(fd, cur, c);
    if (w < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    done += (size_t)w;
  }
  return true;
}

AudiodHello makeHello(const std::string& name, uint8_t priority) {
  AudiodHello h;
  memset(&h, 0, sizeof h);
  h.version = AUDIOD_PROTO_VERSION;
  h.priority = priority;
  h.channels = AUDIOD_CHANNELS;
  h.bits = AUDIOD_BITS;
  h.sampleRate = AUDIOD_SAMPLE_RATE;
  strncpy(h.name, name.c_str(), AUDIOD_NAME_MAX - 1);
  return h;
}

bool parseHello(const Message& m, AudiodHello& out, std::string& why) {
  if (m.type != AUDIOD_MSG_HELLO) { why = "first message is not HELLO"; return false; }
  if (m.payload.size() != sizeof(AudiodHello)) { why = "HELLO size " + std::to_string(m.payload.size()); return false; }
  memcpy(&out, m.payload.data(), sizeof out);
  if (out.version != AUDIOD_PROTO_VERSION) { why = "protocol version " + std::to_string(out.version); return false; }
  if (out.channels != AUDIOD_CHANNELS || out.bits != AUDIOD_BITS || out.sampleRate != AUDIOD_SAMPLE_RATE) {
    why = "unsupported format " + std::to_string(out.sampleRate) + "/" + std::to_string(out.channels) + "/" + std::to_string(out.bits);
    return false;
  }
  if (helloName(out).empty()) { why = "empty source name"; return false; }
  return true;
}

std::string helloName(const AudiodHello& h) {
  char n[AUDIOD_NAME_MAX + 1];
  memcpy(n, h.name, AUDIOD_NAME_MAX);
  n[AUDIOD_NAME_MAX] = 0;
  return n;
}

}  // namespace audiod
