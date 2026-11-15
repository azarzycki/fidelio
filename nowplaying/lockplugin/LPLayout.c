#include "LPLayout.h"

#include <math.h>

int LPIsTablet(float screenW, float screenH) {
  float s = screenW < screenH ? screenW : screenH;
  return s > 600.0f;
}

static LPRect R(float x, float y, float w, float h) {
  LPRect r;
  r.x = x; r.y = y; r.w = w; r.h = h;
  return r;
}

// B1.16.3 frames and fonts, unchanged (device-confirmed on the iPad): the content centred in the lower
// part of the lock screen, below the clock.
static void tablet(float W, float H, LPLayout* L) {
  float cover = W < H ? 220 : 180;  // smaller in landscape (less height)
  float cx = W / 2;
  float top = H * 0.42f;            // below the clock
  float y, tw;
  L->cover = R(cx - cover / 2, top, cover, cover);
  y = top + cover + 14;
  L->source = R(0, y, W, 18); y += 20;
  L->title = R(20, y, W - 40, 32); y += 34;
  L->artist = R(20, y, W - 40, 26); y += 26;
  L->album = R(20, y, W - 40, 22); y += 32;
  tw = W * 0.6f;
  L->track = R(cx - tw / 2, y, tw, 3);
  L->elapsed = R(cx - tw / 2, y + 6, 80, 16);
  L->remaining = R(cx + tw / 2 - 80, y + 6, 80, 16);
  L->idle = R(0, top + cover / 2 - 12, W, 24);
  L->sourceFont = 15; L->titleFont = 26; L->artistFont = 20; L->albumFont = 16; L->timeFont = 13; L->idleFont = 18;
  L->tablet = 1;
}

// The 320x480 phone lock screen: everything between the clock/date area and the slide-to-unlock bar.
static void phone(float W, float H, LPLayout* L) {
  const float gapCover = 8, srcH = 14, titleH = 22, artistH = 18, albumH = 16, gapTrack = 8, trackH = 3, timesGap = 3, timesH = 13;
  const float text = gapCover + srcH + 2 + titleH + 2 + artistH + albumH + gapTrack + trackH + timesGap + timesH;  // 109
  float bandTop = LP_PHONE_BAND_TOP, bandBottom = H - LP_PHONE_BAND_BOTTOM_INSET;
  float band = bandBottom - bandTop;
  float C = band - text - 12;  // keep at least 6 pt above and below the block
  float cx = W / 2, y, coverY, tw;
  if (C > 140) C = 140;
  if (C < 0) C = 0;
  C = floorf(C);
  y = bandTop + floorf((band - (C + text)) / 2);
  if (y < bandTop) y = bandTop;
  coverY = y;
  L->cover = R(floorf(cx - C / 2), y, C, C); y += C + gapCover;
  L->source = R(12, y, W - 24, srcH); y += srcH + 2;
  L->title = R(12, y, W - 24, titleH); y += titleH + 2;
  L->artist = R(12, y, W - 24, artistH); y += artistH;
  L->album = R(12, y, W - 24, albumH); y += albumH + gapTrack;
  tw = floorf(W * 0.6f);
  L->track = R(floorf(cx - tw / 2), y, tw, trackH); y += trackH + timesGap;
  L->elapsed = R(floorf(cx - tw / 2), y, 60, timesH);
  L->remaining = R(floorf(cx + tw / 2) - 60, y, 60, timesH);
  L->idle = R(0, coverY + floorf(C / 2) - 10, W, 20);
  L->sourceFont = 12; L->titleFont = 18; L->artistFont = 15; L->albumFont = 13; L->timeFont = 11; L->idleFont = 15;
  L->tablet = 0;
}

void LPLayoutCompute(float W, float H, int isTablet, LPLayout* out) {
  if (isTablet) tablet(W, H, out);
  else phone(W, H, out);
}
