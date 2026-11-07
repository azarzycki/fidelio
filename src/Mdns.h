#pragma once
// _raop._tcp registration through the system mDNSResponder (dns_sd).
#ifdef __APPLE__
#include <dns_sd.h>
#else
typedef void* DNSServiceRef;  // host test build: no mDNS
#endif
#include <stdint.h>

#include <map>
#include <string>

class RaopAdvertiser {
 public:
  // name: "<MAC>@Fidelio (AirPlay)"; port: the RTSP port.
  bool start(const std::string& instanceName, uint16_t port, const std::map<std::string, std::string>& txt);
  void stop();
  ~RaopAdvertiser() { stop(); }

 private:
  DNSServiceRef ref = nullptr;
  bool active = false;
};
