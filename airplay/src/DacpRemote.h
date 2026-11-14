#pragma once
// B1.12: send a DACP pause to an AirPlay sender that offered remote control (RTSP headers DACP-ID
// and Active-Remote). The sender runs a _dacp._tcp service named "iTunes_Ctrl_<DACP-ID>"; we resolve
// it to host:port (DNSServiceResolve on the device) and GET /ctrl-int/1/pause with the Active-Remote
// header. For host tests, AUDIOD_RAOP_DACP_ADDR=host:port bypasses the mDNS resolve.
#include <stdint.h>

#include <string>

namespace dacp {

// Resolve + send GET /ctrl-int/1/pause with "Active-Remote: <token>". `senderIp` is the RTSP peer
// (used only as a fallback / for logging). Returns true if the request was sent and a response read
// (the reply is treated as "command received", not proof the sender paused). Non-blocking budget
// ~1.5 s total. Never throws.
bool sendPause(const std::string& dacpId, const std::string& activeRemote, const std::string& senderIp);

}  // namespace dacp
