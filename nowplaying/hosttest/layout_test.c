// B1.18 host layout test: runs the Now Playing layout (nowplaying/src/NPLayout.c, the same C the app
// uses) for the phone and the tablet in both orientations, and asserts that every element is on-screen,
// no two elements overlap, the controls are full-size touch targets on the tablet, and the phone
// layout is exactly the B1.16.1 one.
#include <stdio.h>
#include <string.h>

#include "NPLayout.h"

static int fails = 0;
#define CHECK(c, ...) do { if (c) printf("ok   "); else { printf("FAIL "); fails++; } printf(__VA_ARGS__); printf("\n"); } while (0)

static const char* names[] = { "cover", "title", "artist", "album", "progress", "elapsed", "remaining", "state", "prev", "play", "next" };
static void rects(const NPLayout* L, NPRect* r) {
  r[0] = L->cover; r[1] = L->title; r[2] = L->artist; r[3] = L->album; r[4] = L->progress; r[5] = L->elapsed;
  r[6] = L->remaining; r[7] = L->state; r[8] = L->prev; r[9] = L->play; r[10] = L->next;
}
static int overlap(NPRect a, NPRect b) {
  return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}
static int eq(NPRect a, float x, float y, float w, float h) { return a.x == x && a.y == y && a.w == w && a.h == h; }

static void check_size(float W, float H, int tablet, const char* label) {
  NPLayout L;
  NPRect r[11];
  int i, j, off = 0, ov = 0;
  char what[160] = "";
  memset(&L, 0, sizeof L);
  NPLayoutCompute(W, H, tablet, &L);
  rects(&L, r);
  for (i = 0; i < 11; i++) {
    if (r[i].w <= 0 || r[i].h <= 0 || r[i].x < 0 || r[i].y < 0 || r[i].x + r[i].w > W + 0.01f || r[i].y + r[i].h > H + 0.01f) {
      off++;
      snprintf(what, sizeof what, "%s (%.1f,%.1f %.1fx%.1f)", names[i], r[i].x, r[i].y, r[i].w, r[i].h);
    }
  }
  CHECK(off == 0, "%s %gx%g: every element is inside the view%s%s", label, W, H, off ? ", outside: " : "", what);
  what[0] = 0;
  for (i = 0; i < 11; i++)
    for (j = i + 1; j < 11; j++)
      if (overlap(r[i], r[j])) { ov++; snprintf(what, sizeof what, "%s / %s", names[i], names[j]); }
  CHECK(ov == 0, "%s %gx%g: no two elements overlap%s%s", label, W, H, ov ? ", e.g. " : "", what);
  CHECK(L.prev.y + L.prev.h <= H && L.play.y + L.play.h <= H && L.next.y + L.next.h <= H && L.play.y >= 0,
        "%s %gx%g: the control row is on-screen (bottom %.0f <= %.0f)", label, W, H, L.play.y + L.play.h, H);
  if (tablet) {
    CHECK(L.prev.w >= 60 && L.prev.h >= 60 && L.play.w >= 60 && L.play.h >= 60 && L.next.w >= 60 && L.next.h >= 60,
          "%s %gx%g: transport buttons are >= 60 pt touch targets (%.0fx%.0f)", label, W, H, L.play.w, L.play.h);
    CHECK(L.cover.w >= 560 && L.cover.w <= 600 && L.cover.w == L.cover.h, "%s %gx%g: large square cover (%.0f pt)", label, W, H, L.cover.w);
    CHECK(L.sideBySide == (W > H), "%s %gx%g: %s layout", label, W, H, W > H ? "side-by-side (landscape)" : "stacked (portrait)");
  }
}

int main(void) {
  NPLayout L;
  float W = 320, H = 431;
  // tablet detection from the screen size
  CHECK(!NPIsTablet(320, 480) && NPIsTablet(768, 1024) && NPIsTablet(1024, 768), "tablet = a screen whose shorter side exceeds 600 pt");
  CHECK(NPCoverPixels(0) == 256 && NPCoverPixels(1) == 600, "cover decode size: 256 on the phone, 600 on the tablet");
  // phone: exactly the B1.16.1 frames
  NPLayoutCompute(W, H, 0, &L);
  CHECK(eq(L.cover, 32, 4, 256, 256) && eq(L.title, 12, 264, 296, 30) && eq(L.artist, 12, 294, 296, 24) && eq(L.album, 12, 318, 296, 20) &&
        eq(L.progress, 20, 338, 280, 9) && eq(L.elapsed, 20, 350, 110, 16) && eq(L.remaining, 190, 350, 110, 16) && eq(L.state, 14, 368, 292, 16) &&
        eq(L.prev, 50, 386, 60, 40) && eq(L.play, 130, 386, 60, 40) && eq(L.next, 210, 386, 60, 40) &&
        L.titleFont == 26 && L.artistFont == 20 && L.albumFont == 16 && L.timeFont == 15 && L.stateFont == 13 && L.glyphScale == 1,
        "phone 320x431: identical to the B1.16.1 layout (frames and fonts; btnY = H-45, state -18, times -18, progress -12)");
  check_size(320, 431, 0, "phone");
  // tablet: portrait and landscape, with (20 pt) and without the status bar, minus the 49 pt tab bar
  check_size(768, 955, 1, "tablet portrait");
  check_size(768, 975, 1, "tablet portrait, no status bar");
  check_size(1024, 699, 1, "tablet landscape");
  check_size(1024, 719, 1, "tablet landscape, no status bar");
  printf(fails ? "\n%d FAILURES\n" : "\nall layout checks passed\n", fails);
  return fails ? 1 : 0;
}
