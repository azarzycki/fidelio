#include "NPLayout.h"

int NPIsTablet(float screenW, float screenH) {
  float s = screenW < screenH ? screenW : screenH;
  return s > 600.0f;
}

float NPCoverPixels(int tablet) { return tablet ? 600.0f : 256.0f; }

static NPRect R(float x, float y, float w, float h) {
  NPRect r;
  r.x = x; r.y = y; r.w = w; r.h = h;
  return r;
}

static float minf(float a, float b) { return a < b ? a : b; }

// B1.16.1 frames, unchanged (the host test pins them): a 256 cover at the top, three text lines below
// it, the control row 5 pt above the bottom edge, progress / times / state stacked above the controls.
static void phone(float W, float H, NPLayout* L) {
  const float C = 256.0f;
  float y, btnH = 40, btnY, stateY, timesY, progY, cx0 = W * 0.25f, cx1 = W * 0.5f, cx2 = W * 0.75f;
  L->cover = R((W - C) / 2, 4, C, C);
  y = 4 + C + 4;
  L->title = R(12, y, W - 24, 30); y += 30;
  L->artist = R(12, y, W - 24, 24); y += 24;
  L->album = R(12, y, W - 24, 20);
  btnY = H - btnH - 5;
  stateY = btnY - 18; timesY = stateY - 18; progY = timesY - 12;
  L->progress = R(20, progY, W - 40, 9);
  L->elapsed = R(20, timesY, 110, 16);
  L->remaining = R(W - 130, timesY, 110, 16);
  L->state = R(14, stateY, W - 28, 16);
  L->prev = R(cx0 - 30, btnY, 60, btnH);
  L->play = R(cx1 - 30, btnY, 60, btnH);
  L->next = R(cx2 - 30, btnY, 60, btnH);
  L->titleFont = 26; L->artistFont = 20; L->albumFont = 16; L->timeFont = 15; L->stateFont = 13;
  L->glyphScale = 1; L->tablet = 0; L->sideBySide = 0;
}

// Tablet, portrait: the cover (up to 600) centred on top, three text lines, and the control cluster
// anchored to the bottom edge: progress, times, state, then the 96 x 64 buttons (>= 60 pt targets).
static void tabletPortrait(float W, float H, NPLayout* L) {
  const float m = 24, btnW = 96, btnH = 64, textH = 48 + 36 + 30;
  float btnY = H - 16 - btnH, stateY = btnY - 10 - 24, timesY = stateY - 6 - 24, progY = timesY - 6 - 12;
  float C = minf(minf(600, W - 2 * m), progY - 16 - textH - 12 - m), x, y, cx = W / 2;
  if (C < 0) C = 0;
  C = (float)(int)C;
  x = (W - C) / 2;
  L->cover = R(x, m, C, C);
  y = m + C + 12;
  L->title = R(m, y, W - 2 * m, 48); y += 48;
  L->artist = R(m, y, W - 2 * m, 36); y += 36;
  L->album = R(m, y, W - 2 * m, 30);
  L->progress = R(x, progY, C, 12);                      // as wide as the cover
  L->elapsed = R(x, timesY, 160, 24);
  L->remaining = R(x + C - 160, timesY, 160, 24);
  L->state = R(m, stateY, W - 2 * m, 24);
  L->prev = R(cx - 150 - btnW / 2, btnY, btnW, btnH);
  L->play = R(cx - btnW / 2, btnY, btnW, btnH);
  L->next = R(cx + 150 - btnW / 2, btnY, btnW, btnH);
  L->titleFont = 40; L->artistFont = 28; L->albumFont = 22; L->timeFont = 20; L->stateFont = 18;
  L->glyphScale = 2; L->tablet = 1; L->sideBySide = 0;
}

// Tablet, landscape: the cover left, vertically centred; on the right a column with the text at the
// cover's top edge and the control cluster ending at the cover's bottom edge.
static void tabletLandscape(float W, float H, NPLayout* L) {
  const float m = 24, gap = 32, btnW = 96, btnH = 64;
  float C = minf(minf(600, H - 2 * m), W * 0.6f), top, bottom, colX, colW, btnY, stateY, timesY, progY, y;
  if (C < 0) C = 0;
  C = (float)(int)C;
  top = (H - C) / 2; bottom = top + C;
  colX = m + C + gap; colW = W - colX - m;
  L->cover = R(m, top, C, C);
  y = top;
  L->title = R(colX, y, colW, 42); y += 42;
  L->artist = R(colX, y, colW, 32); y += 32;
  L->album = R(colX, y, colW, 26);
  btnY = bottom - btnH; stateY = btnY - 10 - 24; timesY = stateY - 6 - 24; progY = timesY - 6 - 12;
  L->progress = R(colX, progY, colW, 12);
  L->elapsed = R(colX, timesY, 140, 24);
  L->remaining = R(colX + colW - 140, timesY, 140, 24);
  L->state = R(colX, stateY, colW, 24);
  L->prev = R(colX + colW / 6 - btnW / 2, btnY, btnW, btnH);
  L->play = R(colX + colW / 2 - btnW / 2, btnY, btnW, btnH);
  L->next = R(colX + colW * 5 / 6 - btnW / 2, btnY, btnW, btnH);
  L->titleFont = 32; L->artistFont = 24; L->albumFont = 20; L->timeFont = 18; L->stateFont = 16;
  L->glyphScale = 2; L->tablet = 1; L->sideBySide = 1;
}

void NPLayoutCompute(float W, float H, int tablet, NPLayout* out) {
  if (!tablet) phone(W, H, out);
  else if (W > H) tabletLandscape(W, H, out);
  else tabletPortrait(W, H, out);
}
