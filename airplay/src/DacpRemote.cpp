#include "DacpRemote.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "ios3_trace.h"

#ifdef __APPLE__
#include <dns_sd.h>
#endif

namespace dacp {

// Resolve iTunes_Ctrl_<DACP-ID>._dacp._tcp to host:port. Device: DNSServiceResolve (blocking, with a
// short select budget). Host/test: AUDIOD_RAOP_DACP_ADDR=host:port short-circuits it.
static bool resolve(const std::string& dacpId, std::string& host, uint16_t& port) {
  if (const char* ov = getenv("AUDIOD_RAOP_DACP_ADDR")) {
    const char* colon = strrchr(ov, ':');
    if (!colon) return false;
    host.assign(ov, colon - ov);
    port = (uint16_t)atoi(colon + 1);
    return port != 0;
  }
#ifdef __APPLE__
  std::string svc = "iTunes_Ctrl_" + dacpId;
  struct Ctx { bool ok; std::string host; uint16_t port; } ctx = { false, "", 0 };
  DNSServiceRef ref = nullptr;
  DNSServiceErrorType e = DNSServiceResolve(
      &ref, 0, kDNSServiceInterfaceIndexAny, svc.c_str(), "_dacp._tcp", "local.",
      [](DNSServiceRef, DNSServiceFlags, uint32_t, DNSServiceErrorType err, const char*, const char* hosttarget, uint16_t p, uint16_t, const unsigned char*, void* context) {
        Ctx* c = static_cast<Ctx*>(context);
        if (err == kDNSServiceErr_NoError && hosttarget) { c->host = hosttarget; c->port = ntohs(p); c->ok = true; }
      },
      &ctx);
  if (e != kDNSServiceErr_NoError || !ref) return false;
  int fd = DNSServiceRefSockFD(ref);
  struct timeval tv = { 1, 500 * 1000 };
  fd_set r; FD_ZERO(&r); FD_SET(fd, &r);
  if (select(fd + 1, &r, nullptr, nullptr, &tv) > 0) DNSServiceProcessResult(ref);
  DNSServiceRefDeallocate(ref);
  if (!ctx.ok) return false;
  host = ctx.host; port = ctx.port;
  return true;
#else
  (void)dacpId; (void)host; (void)port;
  return false;
#endif
}

static bool connectHost(const std::string& host, uint16_t port, const std::string& fallbackIp, int& fdOut) {
  // Try the resolved host first, then the RTSP peer IP as a fallback (same device, DACP port).
  for (const std::string& h : { host, fallbackIp }) {
    if (h.empty()) continue;
    struct addrinfo hints; memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    char ps[8]; snprintf(ps, sizeof ps, "%u", port);
    struct addrinfo* res = nullptr;
    if (getaddrinfo(h.c_str(), ps, &hints, &res) != 0 || !res) continue;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { freeaddrinfo(res); continue; }
    struct timeval to = { 1, 0 };
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &to, sizeof to);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof to);
    if (connect(fd, res->ai_addr, res->ai_addrlen) == 0) { freeaddrinfo(res); fdOut = fd; return true; }
    close(fd); freeaddrinfo(res);
  }
  return false;
}

static bool get(const std::string& path, const std::string& dacpId, const std::string& activeRemote, const std::string& senderIp) {
  if (dacpId.empty() || activeRemote.empty()) return false;
  std::string host; uint16_t port = 0;
  if (!resolve(dacpId, host, port)) { IOS3_TRACE("DACP: could not resolve iTunes_Ctrl_%s._dacp._tcp", dacpId.c_str()); return false; }
  int fd = -1;
  if (!connectHost(host, port, senderIp, fd)) { IOS3_TRACE("DACP: cannot connect to %s:%u", host.c_str(), port); return false; }
  char req[320];
  int n = snprintf(req, sizeof req,
                   "GET %s HTTP/1.1\r\nActive-Remote: %s\r\nHost: %s:%u\r\nConnection: close\r\n\r\n",
                   path.c_str(), activeRemote.c_str(), host.c_str(), port);
  bool ok = write(fd, req, n) == n;
  int status = 0;
  if (ok) { char resp[256] = {0}; ssize_t r = read(fd, resp, sizeof resp - 1); if (r > 0) sscanf(resp, "HTTP/1.%*d %d", &status); }
  close(fd);
  IOS3_TRACE("DACP: GET %s to %s:%u (Active-Remote %s) -> %s (HTTP %d)", path.c_str(), host.c_str(), port, activeRemote.c_str(), ok ? "sent" : "write failed", status);
  return ok;
}

bool sendPause(const std::string& dacpId, const std::string& activeRemote, const std::string& senderIp) {
  return get("/ctrl-int/1/pause", dacpId, activeRemote, senderIp);
}

bool sendVolume(const std::string& dacpId, const std::string& activeRemote, const std::string& senderIp, float db) {
  char path[96];
  snprintf(path, sizeof path, "/ctrl-int/1/setproperty?dmcp.device-volume=%.6f", db);
  return get(path, dacpId, activeRemote, senderIp);
}

}  // namespace dacp
