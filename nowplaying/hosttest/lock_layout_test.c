// B1.16.7 host test of the lock-screen plugin's layout (nowplaying/lockplugin/LPLayout.c, the same C the
// plugin uses). The tablet is pinned to the B1.16.3 frames (device-confirmed on the iPad). On the
// 320x480 phone every element must stay inside the band between the clock/date area and the
// slide-to-unlock bar, with nothing overlapping. B1.16.3 put the title at y~456 and the artist past
// the bottom of the screen there.
#include <stdio.h>
#include <string.h>

#include "LPLayout.h"

static int fails = 0;
#define CHECK(c, ...) do { if (c) printf("ok   "); else { printf("FAIL "); fails++; } printf(__VA_ARGS__); printf("\n"); } while (0)

static const char* names[] = { "cover", "source", "title", "artist", "album", "track", "elapsed", "remaining" };
static void rects(const LPLayout* L, LPRect* r) {
  r[0] = L->cover; r[1] = L->source; r[2] = L->title; r[3] = L->artist; r[4] = L->album; r[5] = L->track;
  r[6] = L->elapsed; r[7] = L->remaining;
}
static int overlap(LPRect a, LPRect b) { return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h; }
static int eq(LPRect a, float x, float y, float w, float h) { return a.x == x && a.y == y && a.w == w && a.h == h; }

// The B1.16.3 formulas, written out again here, so a change to the tablet path cannot go unnoticed.
static int tabletIsB1163(float W, float H, const LPLayout* L) {
  float cover = W < H ? 220 : 180, cx = W / 2, top = H * 0.42f, y = top + cover + 14, tw = W * 0.6f;
  int ok = eq(L->cover, cx - cover / 2, top, cover, cover);
  ok = ok && eq(L->source, 0, y, W, 18); y += 20;
  ok = ok && eq(L->title, 20, y, W - 40, 32); y += 34;
  ok = ok && eq(L->artist, 20, y, W - 40, 26); y += 26;
  ok = ok && eq(L->album, 20, y, W - 40, 22); y += 32;
  ok = ok && eq(L->track, cx - tw / 2, y, tw, 3) && eq(L->elapsed, cx - tw / 2, y + 6, 80, 16) && eq(L->remaining, cx + tw / 2 - 80, y + 6, 80, 16);
  ok = ok && eq(L->idle, 0, top + cover / 2 - 12, W, 24);
  ok = ok && L->sourceFont == 15 && L->titleFont == 26 && L->artistFont == 20 && L->albumFont == 16 && L->timeFont == 13 && L->idleFont == 18;
  return ok && L->tablet == 1;
}

int main(void) {
  LPLayout L;
  LPRect r[8];
  int i, j, out = 0, ov = 0;
  char what[160] = "";
  float W = 320, H = 480, top = LP_PHONE_BAND_TOP, bottom = H - LP_PHONE_BAND_BOTTOM_INSET;

  CHECK(!LPIsTablet(320, 480) && LPIsTablet(768, 1024) && LPIsTablet(1024, 768), "tablet = a screen whose shorter side exceeds 600 pt");

  memset(&L, 0, sizeof L);
  LPLayoutCompute(768, 1024, 1, &L);
  CHECK(tabletIsB1163(768, 1024, &L), "tablet portrait 768x1024: identical to the B1.16.3 layout (frames and fonts)");
  memset(&L, 0, sizeof L);
  LPLayoutCompute(1024, 768, 1, &L);
  CHECK(tabletIsB1163(1024, 768, &L), "tablet landscape 1024x768: identical to the B1.16.3 layout (frames and fonts)");

  memset(&L, 0, sizeof L);
  LPLayoutCompute(W, H, 0, &L);
  rects(&L, r);
  for (i = 0; i < 8; i++)
    if (r[i].w <= 0 || r[i].h <= 0 || r[i].x < 0 || r[i].x + r[i].w > W || r[i].y < top || r[i].y + r[i].h > bottom) {
      out++;
      snprintf(what, sizeof what, " (%s at %.0f,%.0f %.0fx%.0f)", names[i], r[i].x, r[i].y, r[i].w, r[i].h);
    }
  CHECK(out == 0, "phone 320x480: everything inside the band %.0f..%.0f between the clock and slide-to-unlock%s", top, bottom, what);
  what[0] = 0;
  for (i = 0; i < 8; i++)
    for (j = i + 1; j < 8; j++)
      if (overlap(r[i], r[j])) { ov++; snprintf(what, sizeof what, " (%s / %s)", names[i], names[j]); }
  CHECK(ov == 0, "phone 320x480: no two elements overlap%s", what);
  CHECK(L.cover.w == L.cover.h && L.cover.w >= 100 && L.cover.w <= 140, "phone 320x480: a square cover of 100..140 pt (%.0f)", L.cover.w);
  CHECK(L.cover.x + L.cover.w / 2 == W / 2 && L.track.x + L.track.w / 2 == W / 2, "phone 320x480: cover and progress bar centred");
  CHECK(L.titleFont <= 18 && L.artistFont <= 15 && L.albumFont <= 13 && L.sourceFont <= 12 && L.timeFont <= 11 && L.tablet == 0,
        "phone 320x480: phone font sizes (title %.0f)", L.titleFont);
  CHECK(L.idle.y >= L.cover.y && L.idle.y + L.idle.h <= L.cover.y + L.cover.h, "phone 320x480: the idle label sits on the cover area (shown instead of it)");
  CHECK(L.elapsed.y >= L.track.y + L.track.h && L.elapsed.x == L.track.x && L.remaining.x + L.remaining.w == L.track.x + L.track.w,
        "phone 320x480: elapsed / remaining under the progress bar, aligned to its ends");
  printf(fails ? "\n%d FAILURES\n" : "\nall lock-plugin layout checks passed\n", fails);
  return fails ? 1 : 0;
}
