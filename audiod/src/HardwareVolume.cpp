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

bool MockHardwareVolume::takeChange(float& level) {
  if (!avail || changeFile.empty()) return false;
  FILE* fp = fopen(changeFile.c_str(), "r");
  if (!fp) return false;
  char buf[64] = {0};
  size_t n = fread(buf, 1, sizeof buf - 1, fp);
  fclose(fp);
  buf[n] = 0;
  std::string content(buf);
  if (content.empty() || content == lastChangeContent) return false;
  lastChangeContent = content;
  float v = (float)atof(buf);
  if (v < 0) v = 0;
  if (v > 1) v = 1;
  if (v == lvl) return false;
  lvl = v;
  level = v;
  return true;
}
