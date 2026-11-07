#include "RaopSession.h"

#include <arpa/inet.h>
#include <errno.h>
#ifdef __APPLE__
#include <mach/mach_time.h>
#endif
#include <math.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "StatusFile.h"
#include "ios3_trace.h"

static const size_t kRxBuf = 2048;

uint64_t RaopSession::nowMs() {
#ifdef __APPLE__
  static mach_timebase_info_data_t tb;
  if (tb.denom == 0) mach_timebase_info(&tb);
  uint64_t t = mach_absolute_time();
  return (t / tb.denom) * tb.numer / 1000000ull + ((t % tb.denom) * tb.numer / tb.denom) / 1000000ull;
#else
  struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
#endif
}

// NTP 64-bit timestamp (seconds since 1900 << 32 | fraction) from the wall clock.
uint64_t RaopSession::ntpNow() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  uint64_t sec = (uint64_t)tv.tv_sec + 2208988800ull;
  uint64_t frac = ((uint64_t)tv.tv_usec << 32) / 1000000ull;
  return (sec << 32) | frac;
}

static void put64(uint8_t* p, uint64_t v) { for (int i = 7; i >= 0; i--) { p[i] = (uint8_t)v; v >>= 8; } }
static uint64_t get64(const uint8_t* p) { uint64_t v = 0; for (int i = 0; i < 8; i++) v = (v << 8) | p[i]; return v; }

RaopSession::RaopSession(AudioOutput* o, StatusFile* s, int latencyMs) : out(o), status(s) {
  latencyPackets = latencyMs * 44100 / 1000 / kFramesPerPacket;
  if (latencyPackets < 4) latencyPackets = 4;
  if (latencyPackets > kSlots - 32) latencyPackets = kSlots - 32;
  pthread_mutex_init(&waitMutex, nullptr);
  pthread_cond_init(&waitCond, nullptr);
  slots.resize(kSlots);  // ~360 KB, allocated once per session
  rxBuf = new uint8_t[kRxBuf];
  decodeBuf = new int16_t[kFramesPerPacket * 2 * 2];  // room for a 2x oversized packet
}

RaopSession::~RaopSession() {
  teardown();
  pthread_cond_destroy(&waitCond);
  pthread_mutex_destroy(&waitMutex);
  delete[] rxBuf;
  delete[] decodeBuf;
}

bool RaopSession::configure(Codec c, const std::string& fmtp, const std::vector<uint8_t>& key, const std::vector<uint8_t>& iv) {
  codec = c;
  if (c == CODEC_ALAC) {
    if (!alac.init(fmtp)) return false;
    if (alac.frameLength() > kFramesPerPacket * 2) { IOS3_TRACE("ALAC frame length %u too large", alac.frameLength()); return false; }
  }
  if (!key.empty()) {
    aes.setKey(key, iv);
    IOS3_TRACE("AES-128-CBC payload decryption enabled");
  }
  return true;
}

static int bindUdp(uint16_t& port) {
  int s = socket(AF_INET, SOCK_DGRAM, 0);
  if (s < 0) return -1;
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = INADDR_ANY;
  a.sin_port = 0;
  if (bind(s, (sockaddr*)&a, sizeof a) != 0) { close(s); return -1; }
  socklen_t l = sizeof a;
  getsockname(s, (sockaddr*)&a, &l);
  port = ntohs(a.sin_port);
  int sz = 64 * 1024;
  setsockopt(s, SOL_SOCKET, SO_RCVBUF, &sz, sizeof sz);
  return s;
}

bool RaopSession::startThread(pthread_t& t, void* (*fn)(void*), size_t stack) {
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, stack);
  int rc = pthread_create(&t, &attr, fn, this);
  pthread_attr_destroy(&attr);
  if (rc != 0) IOS3_TRACE("pthread_create failed: %d", rc);
  return rc == 0;
}

bool RaopSession::setup(const sockaddr_in& peer, uint16_t pControl, uint16_t pTiming) {
  peerAddr = peer;
  peerControlPort = pControl;
  peerTimingPort = pTiming;
  audioSock = bindUdp(localAudioPort);
  controlSock = bindUdp(localControlPort);
  timingSock = bindUdp(localTimingPort);
  if (audioSock < 0 || controlSock < 0 || timingSock < 0) { IOS3_TRACE("UDP bind failed: %s", strerror(errno)); closeSockets(); return false; }
  running = true;
  audioThreadUp = startThread(audioThread, audioThreadEntry, 128 * 1024);
  controlThreadUp = startThread(controlThread, controlThreadEntry, 64 * 1024);
  timingThreadUp = startThread(timingThread, timingThreadEntry, 64 * 1024);
  playerThreadUp = startThread(playerThread, playerThreadEntry, 64 * 1024);
  IOS3_TRACE("session UDP ports: audio %u control %u timing %u; peer control %u timing %u; latency %d packets",
             localAudioPort, localControlPort, localTimingPort, pControl, pTiming, latencyPackets);
  return audioThreadUp && playerThreadUp;
}

void RaopSession::record(uint16_t seq, uint32_t rtptime) {
  {
    std::scoped_lock l(bufMutex);
    for (auto& s : slots) s.valid = false;
    expectedSeq = seq; haveExpected = true; haveNewest = false; haveLastRx = false; primed = false;
  }
  playing = true;
  IOS3_TRACE("RECORD: expecting seq %u (rtptime %u)", seq, rtptime);
}

void RaopSession::flush(uint16_t seq, bool haveSeq) {
  {
    std::scoped_lock l(bufMutex);
    for (auto& s : slots) s.valid = false;
    if (haveSeq) { expectedSeq = seq; haveExpected = true; } else haveExpected = false;
    haveNewest = false; haveLastRx = false; primed = false;
  }
  out->flush();
  IOS3_TRACE("FLUSH: buffer dropped, next seq %s%u", haveSeq ? "" : "(unknown) ", seq);
}

void RaopSession::teardown() {
  if (!running) return;
  running = false;
  playing = false;
  closeSockets();  // unblocks the receive threads
  pthread_mutex_lock(&waitMutex);
  pthread_cond_broadcast(&waitCond);
  pthread_mutex_unlock(&waitMutex);
  if (audioThreadUp) pthread_join(audioThread, nullptr);
  if (controlThreadUp) pthread_join(controlThread, nullptr);
  if (timingThreadUp) pthread_join(timingThread, nullptr);
  if (playerThreadUp) pthread_join(playerThread, nullptr);
  audioThreadUp = controlThreadUp = timingThreadUp = playerThreadUp = false;
  out->flush();
  IOS3_TRACE("session torn down: %s", stats().c_str());
}

void RaopSession::closeSockets() {
  if (audioSock >= 0) { shutdown(audioSock, SHUT_RDWR); close(audioSock); audioSock = -1; }
  if (controlSock >= 0) { shutdown(controlSock, SHUT_RDWR); close(controlSock); controlSock = -1; }
  if (timingSock >= 0) { shutdown(timingSock, SHUT_RDWR); close(timingSock); timingSock = -1; }
}

void RaopSession::setVolumeDb(float db) {
  volDb = db;
  float lin = db <= -144.0f ? 0.0f : powf(10.0f, db / 20.0f);  // true dB attenuation, -30 dB = 0.032
  if (lin > 1.0f) lin = 1.0f;
  out->setVolume(lin);
}

bool RaopSession::audioFlowing() {
  uint64_t t = lastPacketMs.load();
  return t != 0 && nowMs() - t < 3000;
}

std::string RaopSession::stats() {
  char b[200];
  snprintf(b, sizeof b, "packets=%u resent=%u lost=%u late=%u decode_err=%u skipped=%u", statPackets.load(), statResent.load(),
           statLost.load(), statLate.load(), statDecodeErr.load(), statSkipped.load());
  return b;
}

void* RaopSession::audioThreadEntry(void* s) { static_cast<RaopSession*>(s)->audioLoop(); return nullptr; }
void* RaopSession::controlThreadEntry(void* s) { static_cast<RaopSession*>(s)->controlLoop(); return nullptr; }
void* RaopSession::timingThreadEntry(void* s) { static_cast<RaopSession*>(s)->timingLoop(); return nullptr; }
void* RaopSession::playerThreadEntry(void* s) { static_cast<RaopSession*>(s)->playerLoop(); return nullptr; }

static bool waitReadable(int fd, int ms) {
  if (fd < 0) return false;
  fd_set rs; FD_ZERO(&rs); FD_SET(fd, &rs);
  struct timeval tv = { ms / 1000, (ms % 1000) * 1000 };
  return select(fd + 1, &rs, nullptr, nullptr, &tv) > 0;
}

void RaopSession::audioLoop() {
  uint8_t* buf = rxBuf;
  bool firstLogged = false;
  while (running) {
    if (!waitReadable(audioSock, 200)) continue;
    sockaddr_in from{}; socklen_t fl = sizeof from;
    ssize_t n = recvfrom(audioSock, buf, kRxBuf, 0, (sockaddr*)&from, &fl);
    if (n <= 0) { if (!running) break; continue; }
    if (n < 12) continue;
    uint8_t type = buf[1] & 0x7f;
    if (type != 0x60) { if (!firstLogged) { IOS3_TRACE("audio port: unexpected packet type 0x%02x", type); firstLogged = true; } continue; }
    if (!firstLogged) { IOS3_TRACE("first audio packet: %d bytes, seq %u", (int)n, (buf[2] << 8) | buf[3]); firstLogged = true; }
    onRtpPacket(buf, (size_t)n, false);
  }
}

void RaopSession::onRtpPacket(const uint8_t* pkt, size_t len, bool resent) {
  uint16_t seq = (uint16_t)((pkt[2] << 8) | pkt[3]);
  size_t payloadLen = len - 12;
  uint8_t* payload = const_cast<uint8_t*>(pkt) + 12;  // decrypt in place inside rxBuf (or the control buffer)
  lastPacketMs = nowMs();
  statPackets++;
  if (resent) statResent++;

  // gap detection -> resend request (control channel), before decoding so it goes out quickly
  if (!resent) {
    std::scoped_lock l(bufMutex);
    if (haveLastRx) {
      int16_t gap = (int16_t)(seq - lastRxSeq);
      if (gap > 1 && gap <= 64) { requestResend((uint16_t)(lastRxSeq + 1), (uint16_t)(gap - 1)); statLost += gap - 1; }
    }
    if (!haveLastRx || (int16_t)(seq - lastRxSeq) > 0) { lastRxSeq = seq; haveLastRx = true; }
  }

  aes.decrypt(payload, payloadLen);

  uint32_t frames = 0;
  if (codec == CODEC_ALAC) {
    frames = alac.decode(payload, payloadLen, decodeBuf, kFramesPerPacket * 2);
    if (frames == 0) { statDecodeErr++; return; }
  } else if (codec == CODEC_PCM) {
    frames = (uint32_t)(payloadLen / 4);
    if (frames > kFramesPerPacket * 2) frames = kFramesPerPacket * 2;
    const uint8_t* p = payload;
    for (uint32_t i = 0; i < frames * 2; i++, p += 2) decodeBuf[i] = (int16_t)((p[0] << 8) | p[1]);  // L16 is big-endian
  } else return;
  if (frames > kFramesPerPacket) frames = kFramesPerPacket;  // a slot holds one 352-frame packet

  std::scoped_lock l(bufMutex);
  if (!haveExpected) { expectedSeq = seq; haveExpected = true; }  // FLUSH without seq / no RECORD yet
  int16_t ahead = (int16_t)(seq - expectedSeq);
  if (ahead < 0) { statLate++; return; }          // already played
  if (ahead >= kSlots) {                           // sender far ahead of us: resync
    for (auto& s : slots) s.valid = false;
    expectedSeq = seq; primed = false; ahead = 0; statSkipped++;
  }
  Slot& sl = slots[seq % kSlots];
  sl.seq = seq; sl.frames = (uint16_t)frames; sl.valid = true;
  memcpy(sl.pcm, decodeBuf, frames * 4);
  if (!haveNewest || (int16_t)(seq - newestSeq) > 0) { newestSeq = seq; haveNewest = true; }
  pthread_mutex_lock(&waitMutex);
  pthread_cond_signal(&waitCond);
  pthread_mutex_unlock(&waitMutex);
}

void RaopSession::requestResend(uint16_t first, uint16_t count) {
  if (controlSock < 0 || peerControlPort == 0) return;
  uint8_t req[8] = { 0x80, 0xd5, 0x00, 0x01, (uint8_t)(first >> 8), (uint8_t)first, (uint8_t)(count >> 8), (uint8_t)count };
  sockaddr_in to = peerAddr;
  to.sin_port = htons(peerControlPort);
  sendto(controlSock, req, sizeof req, 0, (sockaddr*)&to, sizeof to);
}

void RaopSession::controlLoop() {
  uint8_t buf[kRxBuf];
  bool syncLogged = false;
  while (running) {
    if (!waitReadable(controlSock, 200)) continue;
    sockaddr_in from{}; socklen_t fl = sizeof from;
    ssize_t n = recvfrom(controlSock, buf, sizeof buf, 0, (sockaddr*)&from, &fl);
    if (n <= 0) { if (!running) break; continue; }
    if (n < 4) continue;
    uint8_t type = buf[1] & 0x7f;
    if (type == 0x54) {  // sync: rtptime-now-minus-latency[4], ntp[8], rtptime-now[4]; we run on a local clock
      if (!syncLogged && n >= 20) {
        uint32_t rtpLat = (buf[4] << 24) | (buf[5] << 16) | (buf[6] << 8) | buf[7];
        uint32_t rtpNow = (buf[16] << 24) | (buf[17] << 16) | (buf[18] << 8) | buf[19];
        IOS3_TRACE("first sync packet: rtptime %u, sender latency %u frames (%u ms)", rtpNow, rtpNow - rtpLat, (rtpNow - rtpLat) / 44);
        syncLogged = true;
      }
    } else if (type == 0x56) {  // resend response: 4-byte header + full RTP packet
      if (n >= 16) onRtpPacket(buf + 4, (size_t)n - 4, true);
    }
  }
}

void RaopSession::timingLoop() {
  uint8_t buf[64];
  uint64_t lastReq = 0;
  int replies = 0;
  while (running) {
    uint64_t now = nowMs();
    if (peerTimingPort && now - lastReq >= 3000) {  // keep the sender's timing responder exercised; measure RTT
      uint8_t req[32]; memset(req, 0, sizeof req);
      req[0] = 0x80; req[1] = 0xd2; req[2] = 0x00; req[3] = 0x07;
      put64(req + 24, ntpNow());
      sockaddr_in to = peerAddr; to.sin_port = htons(peerTimingPort);
      sendto(timingSock, req, sizeof req, 0, (sockaddr*)&to, sizeof to);
      lastReq = now;
    }
    if (!waitReadable(timingSock, 200)) continue;
    sockaddr_in from{}; socklen_t fl = sizeof from;
    ssize_t n = recvfrom(timingSock, buf, sizeof buf, 0, (sockaddr*)&from, &fl);
    if (n < 32) continue;
    uint8_t type = buf[1] & 0x7f;
    if (type == 0x52) {  // a sender asking us: reply like a receiver clock
      uint8_t resp[32]; memset(resp, 0, sizeof resp);
      resp[0] = 0x80; resp[1] = 0xd3; resp[2] = 0x00; resp[3] = 0x07;
      memcpy(resp + 8, buf + 24, 8);
      uint64_t t = ntpNow(); put64(resp + 16, t); put64(resp + 24, t);
      sendto(timingSock, resp, sizeof resp, 0, (sockaddr*)&from, fl);
    } else if (type == 0x53) {
      uint64_t origin = get64(buf + 8), now2 = ntpNow();
      if (replies++ % 20 == 0) {
        double rtt = (double)(int64_t)(now2 - origin) / 4294967296.0 * 1000.0;
        IOS3_TRACE("timing reply from sender, RTT %.1f ms", rtt);
      }
    }
  }
}

void RaopSession::playerLoop() {
  static int16_t silence[kFramesPerPacket * 2];
  memset(silence, 0, sizeof silence);
  int underrunLogged = 0;
  int16_t* playBuf = decodeBuf + kFramesPerPacket * 2;  // second half of decodeBuf: the player's copy
  while (running) {
    bool play = false, conceal = false;
    uint16_t frames = 0;
    {
      std::unique_lock<std::mutex> l(bufMutex);
      if (playing && haveExpected && haveNewest) {
        int16_t depth = (int16_t)(newestSeq - expectedSeq) + 1;  // packets from expected..newest
        if (!primed && depth >= latencyPackets) {
          primed = true;
          IOS3_TRACE("playback primed with %d packets (%d ms)", depth, depth * 8);
        }
        if (primed) {
          if (depth > kSlots - 8) {  // the sink fell behind by more than the buffer: jump forward
            expectedSeq = (uint16_t)(newestSeq - latencyPackets);
            statSkipped += depth - latencyPackets;
            depth = latencyPackets + 1;
          }
          Slot& sl = slots[expectedSeq % kSlots];
          if (sl.valid && sl.seq == expectedSeq) {
            memcpy(playBuf, sl.pcm, sl.frames * 4);  // copy out under the lock, play without it
            frames = sl.frames;
            sl.valid = false;
            expectedSeq++;
            play = true;
          } else if (depth > 1) {
            conceal = true;  // missing packet but newer ones exist: silence, move on
            expectedSeq++;
          } else {
            // nothing newer yet: underrun; wait for data without advancing, re-prime for a clean resume
            if (underrunLogged < 3) { IOS3_TRACE("underrun at seq %u, waiting", expectedSeq); underrunLogged++; }
            primed = false;
          }
        }
      }
    }
    if (play) { out->write(playBuf, frames); continue; }
    if (conceal) { out->write(silence, kFramesPerPacket); continue; }
    pthread_mutex_lock(&waitMutex);
    struct timeval tv; gettimeofday(&tv, nullptr);
    struct timespec ts; ts.tv_sec = tv.tv_sec; ts.tv_nsec = (tv.tv_usec + 50000) * 1000;
    if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
    pthread_cond_timedwait(&waitCond, &waitMutex, &ts);
    pthread_mutex_unlock(&waitMutex);
  }
}
