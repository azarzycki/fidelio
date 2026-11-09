#pragma once
// Spotify Connect volume (0..65535) -> output gain (linear amplitude, 0..1). B1.7: the Connect
// slider is the receiver's software volume and loudness is perceived logarithmically, so a linear
// map leaves most of the slider nearly inaudible. librespot's default is a log curve over 60 dB.
#include <math.h>
#include <stdint.h>

enum VolumeCurve { VOLUME_LOG, VOLUME_CUBIC, VOLUME_LINEAR };

static const float kVolumeLogRangeDb = 60.0f;  // slider bottom (just above 0) = -60 dB, top = unity

inline float connectVolumeToGain(uint16_t v, VolumeCurve curve) {
  if (v == 0) return 0.0f;
  float x = v / 65535.0f;
  switch (curve) {
    case VOLUME_CUBIC: return x * x * x;
    case VOLUME_LINEAR: return x;
    case VOLUME_LOG:
    default: return powf(10.0f, (x - 1.0f) * kVolumeLogRangeDb / 20.0f);
  }
}

inline const char* volumeCurveName(VolumeCurve c) { return c == VOLUME_CUBIC ? "cubic" : c == VOLUME_LINEAR ? "linear" : "log"; }
