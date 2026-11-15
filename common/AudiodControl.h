#pragma once
// B1.16: a one-shot transport-control client for fidelio-audiod. A controller (the NowPlaying app or
// the DockRemote tweak) connects to audiod's socket, HELLOs as the non-producing source "control",
// sends one CONTROL verb and closes. audiod routes the verb to the active source. Everything here is
// short-timeout and never blocks the caller for long (safe to call from the app's main thread or,
// via the C wrapper, from inside iapd). Returns true if the command was sent.
#include <stdint.h>
#include <string>

namespace audiod {
bool sendControl(const std::string& sockPath, uint8_t verb, int timeoutMs = 300);
}

// C wrapper so the Objective-C app and the C/C++ tweak can call it without C++ linkage fuss.
#ifdef __cplusplus
extern "C" {
#endif
int audiod_send_control(const char* sockPath, int verb, int timeoutMs);
#ifdef __cplusplus
}
#endif
