#include "RtspServer.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>

#include "Dmap.h"
#include "StatusFile.h"
#include "ios3_trace.h"

static std::string lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }
static std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
  return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

std::string RtspRequest::header(const char* n) const {
  auto it = headers.find(lower(n));
  return it == headers.end() ? "" : it->second;
}

RtspServer::RtspServer(uint16_t p, const std::string& n, const uint8_t hw[6], AudioOutput* o, StatusFile* s, int lat)
    : port(p), name(n), out(o), status(s), latencyMs(lat) {
  memcpy(hwaddr, hw, 6);
}

uint16_t RtspServer::listen() {
  listenFd = socket(AF_INET, SOCK_STREAM, 0);
  if (listenFd < 0) return 0;
  int on = 1;
  setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
#ifdef SO_REUSEPORT
  setsockopt(listenFd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof on);
#endif
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = INADDR_ANY;
  a.sin_port = htons(port);
  if (bind(listenFd, (sockaddr*)&a, sizeof a) != 0) { IOS3_TRACE("RTSP bind %u failed: %s", port, strerror(errno)); close(listenFd); listenFd = -1; return 0; }
  if (::listen(listenFd, 4) != 0) { close(listenFd); listenFd = -1; return 0; }
  socklen_t l = sizeof a;
  getsockname(listenFd, (sockaddr*)&a, &l);
  port = ntohs(a.sin_port);
  return port;
}

void RtspServer::run() {
  while (true) {
    sockaddr_in peer{}; socklen_t pl = sizeof peer;
    int fd = accept(listenFd, (sockaddr*)&peer, &pl);
    if (fd < 0) { if (errno == EINTR) continue; IOS3_TRACE("accept failed: %s", strerror(errno)); sleep(1); continue; }
    int on = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof on);
    Conn* c = new Conn();
    c->fd = fd; c->peer = peer; c->server = this;
    socklen_t ll = sizeof c->local;
    getsockname(fd, (sockaddr*)&c->local, &ll);
    IOS3_TRACE("RTSP connection from %s:%u", inet_ntoa(peer.sin_addr), ntohs(peer.sin_port));
    pthread_t t;
    pthread_attr_t attr; pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 128 * 1024);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&t, &attr, connEntry, c) != 0) { close(fd); delete c; }
    pthread_attr_destroy(&attr);
  }
}

void* RtspServer::connEntry(void* arg) {
  Conn* c = static_cast<Conn*>(arg);
  c->server->handleConnection(c);
  return nullptr;
}

bool RtspServer::readRequest(int fd, std::string& pending, RtspRequest& req) {
  char buf[1024];
  while (true) {
    size_t hdrEnd = pending.find("\r\n\r\n");
    if (hdrEnd != std::string::npos) {
      std::string head = pending.substr(0, hdrEnd);
      size_t p = 0, nl = head.find("\r\n");
      std::string line = head.substr(0, nl);
      size_t s1 = line.find(' '), s2 = line.find(' ', s1 + 1);
      if (s1 == std::string::npos) return false;
      req.method = line.substr(0, s1);
      req.uri = s2 == std::string::npos ? line.substr(s1 + 1) : line.substr(s1 + 1, s2 - s1 - 1);
      req.version = s2 == std::string::npos ? "" : line.substr(s2 + 1);
      req.headers.clear();
      p = nl == std::string::npos ? head.size() : nl + 2;
      while (p < head.size()) {
        size_t e = head.find("\r\n", p);
        if (e == std::string::npos) e = head.size();
        std::string hl = head.substr(p, e - p);
        size_t c = hl.find(':');
        if (c != std::string::npos) req.headers[lower(trim(hl.substr(0, c)))] = trim(hl.substr(c + 1));
        p = e + 2;
      }
      size_t len = (size_t)atoi(req.header("Content-Length").c_str());
      if (len > 1024 * 1024) return false;
      size_t need = hdrEnd + 4 + len;
      while (pending.size() < need) {
        ssize_t n = recv(fd, buf, sizeof buf, 0);
        if (n <= 0) return false;
        pending.append(buf, (size_t)n);
      }
      req.body.assign(pending.begin() + (long)(hdrEnd + 4), pending.begin() + (long)need);
      pending.erase(0, need);
      return true;
    }
    if (pending.size() > 64 * 1024) return false;
    ssize_t n = recv(fd, buf, sizeof buf, 0);
    if (n <= 0) return false;
    pending.append(buf, (size_t)n);
  }
}

void RtspServer::send(int fd, const RtspRequest& req, int code, const std::map<std::string, std::string>& hdr,
                      const std::string& body, const char* contentType) {
  const char* reason = code == 200 ? "OK" : code == 453 ? "Not Enough Bandwidth" : code == 400 ? "Bad Request" : code == 404 ? "Not Found" : "Error";
  std::string r = "RTSP/1.0 " + std::to_string(code) + " " + reason + "\r\n";
  r += "CSeq: " + req.header("CSeq") + "\r\n";
  r += "Server: AirTunes/105.1\r\n";
  for (auto& kv : hdr) r += kv.first + ": " + kv.second + "\r\n";
  if (!body.empty()) {
    r += std::string("Content-Type: ") + (contentType ? contentType : "text/parameters") + "\r\n";
    r += "Content-Length: " + std::to_string(body.size()) + "\r\n";
  }
  r += "\r\n" + body;
  ::send(fd, r.data(), r.size(), 0);
  std::string hs;
  for (auto& kv : hdr) hs += " " + kv.first + "=" + kv.second;
  IOS3_TRACE("  -> %d%s", code, hs.c_str());
}

void RtspServer::handleConnection(Conn* c) {
  std::string pending;
  RtspRequest req;
  while (readRequest(c->fd, pending, req)) {
    std::string hs;
    for (auto& kv : req.headers) if (kv.first != "cseq") hs += " " + kv.first + "=" + kv.second;
    IOS3_TRACE("%s %s (CSeq %s, %d body bytes)%s", req.method.c_str(), req.uri.c_str(), req.header("CSeq").c_str(), (int)req.body.size(),
               hs.c_str());
    handle(c, req);
    if (req.method == "TEARDOWN") break;
  }
  endSession(c, "connection closed");
  close(c->fd);
  IOS3_TRACE("RTSP connection from %s ended", inet_ntoa(c->peer.sin_addr));
  delete c;
}

void RtspServer::endSession(Conn* c, const char* why) {
  std::scoped_lock l(sessionMutex);
  if (sessionOwner == c && session) {
    IOS3_TRACE("ending session (%s)", why);
    session->teardown();
    session.reset();
    sessionOwner = nullptr;
    status->setPlaying(false);
    status->setState("stopped");
  }
  c->ownsSession = false;
}

void RtspServer::handle(Conn* c, const RtspRequest& req) {
  std::map<std::string, std::string> h;
  int code = 200;
  std::string body;
  const char* ctype = nullptr;

  std::string chall = req.header("Apple-Challenge");
  if (!chall.empty()) {
    uint8_t ip[4];
    memcpy(ip, &c->local.sin_addr.s_addr, 4);
    std::string resp = crypto.appleResponse(chall, ip, hwaddr);
    if (!resp.empty()) h["Apple-Response"] = resp;
    else IOS3_TRACE("could not compute Apple-Response");
  }

  const std::string& m = req.method;
  if (m == "OPTIONS") {
    h["Public"] = "ANNOUNCE, SETUP, RECORD, PAUSE, FLUSH, TEARDOWN, OPTIONS, GET_PARAMETER, SET_PARAMETER, POST, GET";
  } else if (m == "ANNOUNCE") {
    onAnnounce(c, req, code, h);
  } else if (m == "SETUP") {
    onSetup(c, req, code, h);
  } else if (m == "RECORD") {
    onRecord(c, req, code, h);
  } else if (m == "FLUSH") {
    onFlush(c, req, code, h);
  } else if (m == "PAUSE") {
    std::scoped_lock l(sessionMutex);
    if (sessionOwner == c && session) { session->flush(0, false); status->setPlaying(false); status->setState("paused"); }
  } else if (m == "SET_PARAMETER") {
    onSetParameter(c, req, code);
  } else if (m == "GET_PARAMETER") {
    std::string b(req.body.begin(), req.body.end());
    if (b.find("volume") != std::string::npos) {
      char v[64];
      snprintf(v, sizeof v, "\r\nvolume: %.6f\r\n", lastVolumeDb);
      body = v; ctype = "text/parameters";
    }
  } else if (m == "TEARDOWN") {
    onTeardown(c);
  } else if (m == "POST" || m == "GET") {
    // /auth-setup (MFi) and friends: we advertise et=0,1 only, answer 200 so senders carry on
    if (req.uri.find("auth-setup") != std::string::npos) IOS3_TRACE("auth-setup requested; not supported (et=0,1), replying 200 with empty body");
    else code = 404;
  } else {
    code = 400;
  }
  send(c->fd, req, code, h, body, ctype);
}

static std::string sdpAttr(const std::string& sdp, const char* key) {  // "a=key:value"
  std::string k = std::string("a=") + key + ":";
  size_t p = sdp.find(k);
  if (p == std::string::npos) return "";
  size_t e = sdp.find_first_of("\r\n", p);
  return trim(sdp.substr(p + k.size(), e == std::string::npos ? std::string::npos : e - p - k.size()));
}

void RtspServer::onAnnounce(Conn* c, const RtspRequest& req, int& code, std::map<std::string, std::string>& h) {
  std::string sdp(req.body.begin(), req.body.end());
  std::string rtpmap = sdpAttr(sdp, "rtpmap"), fmtp = sdpAttr(sdp, "fmtp"), rsaaeskey = sdpAttr(sdp, "rsaaeskey"), aesiv = sdpAttr(sdp, "aesiv");
  IOS3_TRACE("ANNOUNCE sdp: rtpmap='%s' fmtp='%s' encrypted=%s", rtpmap.c_str(), fmtp.c_str(), rsaaeskey.empty() ? "no" : "yes");

  RaopSession::Codec codec = RaopSession::CODEC_NONE;
  std::string fmtpParams;
  if (rtpmap.find("AppleLossless") != std::string::npos) {
    codec = RaopSession::CODEC_ALAC;
    size_t sp = fmtp.find(' ');
    fmtpParams = sp == std::string::npos ? "" : fmtp.substr(sp + 1);
  } else if (rtpmap.find("L16") != std::string::npos) {
    codec = RaopSession::CODEC_PCM;
  } else {
    IOS3_TRACE("unsupported codec in ANNOUNCE");
    code = 400; return;
  }

  std::vector<uint8_t> key, iv;
  if (!rsaaeskey.empty()) {
    if (!crypto.unwrapAesKey(rsaaeskey, key)) { code = 400; return; }
    iv = RaopCrypto::b64decode(aesiv);
    if (iv.size() != 16) { IOS3_TRACE("bad aesiv"); code = 400; return; }
  }

  std::scoped_lock l(sessionMutex);
  if (session && sessionOwner != c) {
    IOS3_TRACE("busy: another sender has the session -> 453");
    code = 453; return;
  }
  if (session) { session->teardown(); session.reset(); }  // re-ANNOUNCE on the same connection
  session.reset(new RaopSession(out, status, latencyMs));
  if (!session->configure(codec, fmtpParams, key, iv)) { session.reset(); code = 400; return; }
  sessionOwner = c;
  c->ownsSession = true;
  c->sessionUri = req.uri;
  session->setVolumeDb(lastVolumeDb);
  status->clearTrack();
  status->setCodec(session->codecName());
  status->setPeer(std::string(inet_ntoa(c->peer.sin_addr)) + " " + req.header("User-Agent"));
  status->setState("connected");
}

void RtspServer::onSetup(Conn* c, const RtspRequest& req, int& code, std::map<std::string, std::string>& h) {
  std::scoped_lock l(sessionMutex);
  if (!session || sessionOwner != c) { code = 400; return; }
  std::string t = req.header("Transport");
  uint16_t cport = 0, tport = 0;
  size_t p = t.find("control_port=");
  if (p != std::string::npos) cport = (uint16_t)atoi(t.c_str() + p + 13);
  p = t.find("timing_port=");
  if (p != std::string::npos) tport = (uint16_t)atoi(t.c_str() + p + 12);
  if (!session->setup(c->peer, cport, tport)) { code = 400; return; }
  char tr[200];
  snprintf(tr, sizeof tr, "RTP/AVP/UDP;unicast;interleaved=0-1;mode=record;control_port=%u;timing_port=%u;server_port=%u",
           session->controlPort(), session->timingPort(), session->audioPort());
  h["Transport"] = tr;
  h["Session"] = "1";
  h["Audio-Jitter-Latency"] = "0";
}

void RtspServer::onRecord(Conn* c, const RtspRequest& req, int& code, std::map<std::string, std::string>& h) {
  std::scoped_lock l(sessionMutex);
  if (!session || sessionOwner != c) { code = 400; return; }
  std::string info = req.header("RTP-Info");
  uint16_t seq = 0; uint32_t rtptime = 0;
  size_t p = info.find("seq=");
  if (p != std::string::npos) seq = (uint16_t)atoi(info.c_str() + p + 4);
  p = info.find("rtptime=");
  if (p != std::string::npos) rtptime = (uint32_t)strtoul(info.c_str() + p + 8, nullptr, 10);
  session->record(seq, rtptime);
  h["Audio-Latency"] = "11025";
  status->setPlaying(true);
  status->setState("playing");
}

void RtspServer::onFlush(Conn* c, const RtspRequest& req, int& code, std::map<std::string, std::string>& h) {
  std::scoped_lock l(sessionMutex);
  if (!session || sessionOwner != c) { code = 400; return; }
  std::string info = req.header("RTP-Info");
  size_t p = info.find("seq=");
  bool haveSeq = p != std::string::npos;
  uint16_t seq = haveSeq ? (uint16_t)atoi(info.c_str() + p + 4) : 0;
  session->flush(seq, haveSeq);
  status->setPlaying(false);
  status->setState("paused");
}

void RtspServer::onTeardown(Conn* c) {
  endSession(c, "TEARDOWN");
}

void RtspServer::onSetParameter(Conn* c, const RtspRequest& req, int& code) {
  std::string ct = lower(req.header("Content-Type"));
  if (ct.find("text/parameters") != std::string::npos) {
    std::string b(req.body.begin(), req.body.end());
    size_t p = 0;
    while (p < b.size()) {
      size_t e = b.find('\n', p);
      std::string line = trim(b.substr(p, e == std::string::npos ? std::string::npos : e - p));
      p = e == std::string::npos ? b.size() : e + 1;
      if (line.compare(0, 7, "volume:") == 0) {
        float db = (float)atof(line.c_str() + 7);
        lastVolumeDb = db;
        std::scoped_lock l(sessionMutex);
        if (session && sessionOwner == c) session->setVolumeDb(db);
        IOS3_TRACE("volume %.2f dB", db);
      } else if (line.compare(0, 9, "progress:") == 0) {
        unsigned long start = 0, cur = 0, end = 0;
        if (sscanf(line.c_str() + 9, " %lu/%lu/%lu", &start, &cur, &end) == 3) {
          uint32_t pos = (uint32_t)(((uint32_t)(cur - start)) / 44.1), dur = (uint32_t)(((uint32_t)(end - start)) / 44.1);
          status->setProgress(pos, dur);
          IOS3_TRACE("progress %u / %u ms", pos, dur);
        }
      } else if (!line.empty()) {
        IOS3_TRACE("unhandled parameter: %s", line.c_str());
      }
    }
  } else if (ct.find("application/x-dmap-tagged") != std::string::npos) {
    auto tags = dmapParse(req.body.data(), req.body.size());
    std::string title = tags.count("minm") ? tags["minm"] : "", artist = tags.count("asar") ? tags["asar"] : "",
                album = tags.count("asal") ? tags["asal"] : "";
    status->setTrack(title, artist, album);
    IOS3_TRACE("metadata: '%s' - '%s' (%s) [%d tags]", artist.c_str(), title.c_str(), album.c_str(), (int)tags.size());
  } else if (ct.find("image/jpeg") != std::string::npos || ct.find("image/png") != std::string::npos) {
    status->setCover(req.body);
    IOS3_TRACE("artwork: %d bytes (%s)", (int)req.body.size(), ct.c_str());
  } else if (ct.find("image/none") != std::string::npos) {
    status->setCover({});
  } else {
    IOS3_TRACE("SET_PARAMETER with unhandled Content-Type '%s' (%d bytes)", ct.c_str(), (int)req.body.size());
  }
  (void)code;
}
