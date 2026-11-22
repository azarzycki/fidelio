#include "HardwareVolume.h"

#include <stdio.h>
#include <stdlib.h>

#include "ios3_trace.h"

MockHardwareVolume::MockHardwareVolume() {
  const char* e = getenv("AUDIOD_MOCK_HW_VOLUME");
  if (e && *e) { avail = true; lvl = (float)atof(e); if (lvl < 0) lvl = 0; if (lvl > 1) lvl = 1; }
  const char* f = getenv("AUDIOD_MOCK_HW_CHANGE_FILE");
  if (f && *f) changeFile = f;
  const char* i = getenv("AUDIOD_MOCK_IAP");
  if (i && *i) iapMode = i;
  const char* pm = getenv("AUDIOD_MOCK_HW_POLL_MS");
  if (pm && *pm && atoi(pm) > 0) pollMs = (unsigned)atoi(pm);
}

// B1.16.12: the host stand-in for IAP.framework (see HardwareVolume.h).
HardwareVolume::DockResult MockHardwareVolume::tellDock(float level) {
  if (iapMode.empty()) return DOCK_UNAVAILABLE;
  if (iapMode == "query-fail") return DOCK_QUERY_FAILED;
  if (iapMode == "unsupported") return DOCK_UNSUPPORTED;
  bool ok = iapMode != "fail";
  IOS3_TRACE("mock iap: IAPAudioSetVolume(%.3f) -> %d", level, ok ? 1 : 0);
  return ok ? DOCK_OK : DOCK_FAILED;
}

std::string MockHardwareVolume::describeDock() {
  return iapMode.empty() ? "unavailable (mock: AUDIOD_MOCK_IAP unset)" : "available (mock IAP, AUDIOD_MOCK_IAP=" + iapMode + ")";
}

bool MockHardwareVolume::set(float level) {
  if (!avail) return false;
  lvl = level < 0 ? 0.f : level > 1 ? 1.f : level;
  sets++;
  return true;
}

// B1.16.14: a new value in the change file is the hardware changing: get() sees it at once, takeChange()
// reports it once (the notification), at the next poll.
void MockHardwareVolume::syncChangeFile() {
  if (!avail || changeFile.empty()) return;
  FILE* fp = fopen(changeFile.c_str(), "r");
  if (!fp) return;
  char buf[64] = {0};
  size_t n = fread(buf, 1, sizeof buf - 1, fp);
  fclose(fp);
  buf[n] = 0;
  std::string content(buf);
  if (content.empty() || content == lastChangeContent) return;
  lastChangeContent = content;
  float v = (float)atof(buf);
  if (v < 0) v = 0;
  if (v > 1) v = 1;
  if (v == lvl) return;
  lvl = v;
  changePending = true;
  changeLevel = v;
}

bool MockHardwareVolume::takeChange(float& level) {
  syncChangeFile();
  if (!changePending) return false;
  changePending = false;
  level = changeLevel;
  return true;
}
