#ifndef NPLAYOUT_H
#define NPLAYOUT_H
// B1.18: the Now Playing tab's layout as plain C (no UIKit), so the same function lays out the view on
// the device and runs in the host layout test. Units are points; the origin is the view's top-left.
// The phone result is pinned to the B1.16.1 frames exactly (320 wide); the tablet result is computed
// from the real view size, portrait (cover on top) or landscape (cover left, text + controls right).

typedef struct { float x, y, w, h; } NPRect;

typedef struct {
  NPRect cover, title, artist, album, progress, elapsed, remaining, state, prev, play, next;
  float titleFont, artistFont, albumFont, timeFont, stateFont;  // point sizes (title and times bold)
  float glyphScale;  // transport glyph scale (1 = the 36x30 phone glyph)
  int tablet;        // 1: tablet layout
  int sideBySide;    // 1: cover left, text and controls right (tablet landscape)
} NPLayout;

// A screen whose shorter side is wider than 600 pt is a tablet (iPad 768x1024; iPhone 320x480).
int NPIsTablet(float screenW, float screenH);
// The cover edge to decode for this device: 256 on the phone (memory), 600 on the tablet.
float NPCoverPixels(int tablet);
// Lay out a W x H Now Playing view.
void NPLayoutCompute(float W, float H, int tablet, NPLayout* out);

#endif
