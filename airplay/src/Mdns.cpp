#include "Mdns.h"

#include <arpa/inet.h>

#include "ios3_trace.h"

#ifndef __APPLE__
bool RaopAdvertiser::start(const std::string& name, uint16_t port, const std::map<std::string, std::string>&) {
  IOS3_TRACE("host build: no mDNS; would advertise '%s' on %u", name.c_str(), port);
  return false;
}
void RaopAdvertiser::stop() {}
#else

static void regCallback(DNSServiceRef, DNSServiceFlags, DNSServiceErrorType err, const char* name, const char* type,
                        const char* domain, void*) {
  IOS3_TRACE("mDNS registration callback: err=%d name='%s' type=%s domain=%s", (int)err, name ? name : "", type ? type : "",
             domain ? domain : "");
}

bool RaopAdvertiser::start(const std::string& name, uint16_t port, const std::map<std::string, std::string>& txt) {
  stop();
  TXTRecordRef rec;
  TXTRecordCreate(&rec, 0, nullptr);
  for (auto& kv : txt) TXTRecordSetValue(&rec, kv.first.c_str(), (uint8_t)kv.second.size(), kv.second.c_str());
  DNSServiceErrorType err = DNSServiceRegister(&ref, 0, 0, name.c_str(), "_raop._tcp", nullptr, nullptr, htons(port),
                                               TXTRecordGetLength(&rec), TXTRecordGetBytesPtr(&rec), regCallback, nullptr);
  TXTRecordDeallocate(&rec);
  if (err != kDNSServiceErr_NoError) {
    IOS3_TRACE("DNSServiceRegister(_raop._tcp '%s' port %u) failed: %d", name.c_str(), port, (int)err);
    ref = nullptr;
    return false;
  }
  active = true;
  IOS3_TRACE("advertising _raop._tcp '%s' on port %u (%d TXT entries)", name.c_str(), port, (int)txt.size());
  return true;
}

void RaopAdvertiser::stop() {
  if (active && ref) DNSServiceRefDeallocate(ref);
  ref = nullptr;
  active = false;
}
#endif  // __APPLE__
