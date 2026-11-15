#ifndef LPLAYOUT_H
#define LPLAYOUT_H
// B1.16.7: the lock-screen plugin's layout as plain C (no UIKit), so the same function lays out the
// plugin inside SpringBoard and runs in the host test (nowplaying/hosttest/lock_layout_test.c).
// Units are points; the origin is the plugin view's top-left. The plugin is not full-screen: the
// system clock/date stays at the top and slide-to-unlock at the bottom, so the content goes between.
//  - tablet (screen's shorter side > 600, the iPad): exactly the B1.16.3 layout, which the device
//    photo showed right (the test pins it);
//  - phone (the 320x480 iPhone OS 3.1 lock screen): smaller fonts and a cover of at most 140, the whole
//    block centred in the band between the clock/date area and the slide-to-unlock bar. B1.16.3 used
//    the tablet numbers there, which put the title at y~456 and the artist below the screen.

typedef struct { float x, y, w, h; } LPRect;

typedef struct {
  LPRect cover, source, title, artist, album, track, elapsed, remaining, idle;
  float sourceFont, titleFont, artistFont, albumFont, timeFont, idleFont;  // point sizes (title bold)
  int tablet;
} LPLayout;

// The phone's free band: the iPhone OS 3.1 lock screen's status bar + clock/date area end about this
// far down, and its slide-to-unlock bar takes about this much at the bottom.
#define LP_PHONE_BAND_TOP 116.0f
#define LP_PHONE_BAND_BOTTOM_INSET 96.0f

// A screen whose shorter side is wider than 600 pt is a tablet (iPad 768x1024; iPhone 320x480).
int LPIsTablet(float screenW, float screenH);
// Lay out a W x H plugin view.
void LPLayoutCompute(float W, float H, int tablet, LPLayout* out);

#endif
