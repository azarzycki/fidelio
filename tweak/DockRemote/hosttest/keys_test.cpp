// B1.16.13, B1.16.15 host test of the DockRemote key decode, the swallow decisions and the key log
// (tweak/DockRemote/DockRemoteKeys.cpp, the same C++ the tweak runs inside iapd). The records are built
// the way the phone sent them on 2026-11-22 (remote-events.log of the second observe pass).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "audiod/proto.h"
#include "tweak/DockRemote/DockRemoteKeys.h"

using namespace dockremote;

static int fails = 0;
#define CHECK(c, ...) do { if (c) printf("ok   "); else { printf("FAIL "); fails++; } printf(__VA_ARGS__); printf("\n"); } while (0)

struct Rec { unsigned char b[60]; };
static void put32(unsigned char* p, unsigned off, uint32_t v) { memcpy(p + off, &v, 4); }
static Rec rec(uint32_t type, uint32_t flags, uint32_t code, uint32_t infoSize = 8, uint32_t word52 = 0) {
  Rec r; memset(r.b, 0, sizeof r.b);
  put32(r.b, 0, type); put32(r.b, 40, flags); put32(r.b, 48, infoSize); put32(r.b, 52, word52); put32(r.b, 56, code);
  return r;
}
static Rec down(uint32_t code) { return rec(4001, 0x400, code); }
static Rec up(uint32_t code) { return rec(4001, 0x800, code); }
static Rec ev101() { return rec(101, 0, 0); }

// A stand-in for the hook: the same steps new_GSSendEvent takes for one record (flags given).
struct Hook {
  PressTracker t;
  bool observe = false, swallow = false, conditions = true;  // conditions: what shouldForward() would say
  int asked = 0;                       // how often the conditions were consulted
  std::vector<int> verbs;              // verbs sent to audiod
  std::vector<std::string> lines;      // remote-keys.log lines (time fixed at 1000)
  int passed = 0, dropped = 0;
  static bool mayTake(uint32_t, void* ctx) { Hook* h = (Hook*)ctx; h->asked++; return h->swallow && h->conditions; }
  void send(const Rec& r) {
    KeyEvent k = decodeKey(r.b);
    if (k.valid && (observe || swallow || t.pendingTaken())) {
      Decision d = t.onKey(k, mayTake, this);
      if (d.verb) verbs.push_back(d.verb);
      if (d.logLine) { char l[96]; formatKeyLine(l, sizeof l, 1000, k.code, d.taken); lines.push_back(l); }
      if (d.swallow) { dropped++; return; }
    }
    passed++;
  }
};

static std::string join(const std::vector<std::string>& v) { std::string s; for (auto& x : v) s += x + "|"; return s; }

int main() {
  umask(022);
  // ---- decode: each measured row ----
  struct { Rec r; bool down; uint32_t code; const char* what; } rows[] = {
    { down(0), true, 0, "play/pause 0x400" }, { up(0), false, 0, "play/pause 0x800" },
    { down(22), true, 22, "menu 0x400" },     { up(22), false, 22, "menu 0x800" },
    { down(23), true, 23, "select 0x400" },   { up(23), false, 23, "select 0x800" },
    { down(24), true, 24, "up 0x400" },       { up(24), false, 24, "up 0x800" },
    { down(25), true, 25, "down 0x400" },     { up(25), false, 25, "down 0x800" },
    { up(20), false, 20, "next: one 0x800 event, code 20" },
    { up(21), false, 21, "previous: one 0x800 event, code 21" },
    { up(4), false, 4, "previous: one 0x800 event, code 4 (measured)" },
    { up(3), false, 3, "next: one 0x800 event, code 3 (assumed by symmetry)" },
  };
  for (auto& x : rows) {
    KeyEvent k = decodeKey(x.r.b);
    CHECK(k.valid && k.down == x.down && k.code == x.code, "decode %s: valid, %s, code %u", x.what, x.down ? "down" : "up", x.code);
  }
  // ---- not keys: they always pass ----
  Rec r101 = ev101();
  CHECK(!decodeKey(r101.b).valid, "type 101 (flags 0, info zero) is not a key");
  Rec r4000 = rec(4000, 0, 0); r4000.b[52] = 0x1d; r4000.b[53] = 0x04;
  CHECK(!decodeKey(r4000.b).valid, "type 4000 is not a key");
  Rec bad1 = rec(4001, 0x400, 0, 12), bad2 = rec(4001, 0, 0), bad3 = rec(4001, 0xc00, 0), bad4 = rec(4001, 0x401, 0),
      bad5 = rec(4001, 0x800, 0, 8, 0x16), bad6 = rec(4001, 0x800, 20, 8, 1u << 16);
  CHECK(!decodeKey(bad1.b).valid, "4001 with infoSize 12: unknown shape, not decoded");
  CHECK(!decodeKey(bad2.b).valid && !decodeKey(bad3.b).valid && !decodeKey(bad4.b).valid,
        "4001 with flags 0, 0xc00 or 0x401: unknown shape, not decoded");
  CHECK(!decodeKey(bad5.b).valid && !decodeKey(bad6.b).valid, "4001 with a non-zero word @52 (the old key field): unknown shape");
  CHECK(!decodeKey(nullptr).valid, "a null record is not a key");

  // ---- names and verbs ----
  char nb[24];
  CHECK(!strcmp(keyName(0, nb, sizeof nb), "play-pause") && !strcmp(keyName(20, nb, sizeof nb), "next") &&
        !strcmp(keyName(21, nb, sizeof nb), "previous") && !strcmp(keyName(22, nb, sizeof nb), "menu") &&
        !strcmp(keyName(23, nb, sizeof nb), "select") && !strcmp(keyName(24, nb, sizeof nb), "up") &&
        !strcmp(keyName(25, nb, sizeof nb), "down") && !strcmp(keyName(3, nb, sizeof nb), "next") &&
        !strcmp(keyName(4, nb, sizeof nb), "previous"), "names: play-pause next previous menu select up down; 3 next, 4 previous");
  CHECK(!strcmp(keyName(7, nb, sizeof nb), "code-7") && !strcmp(keyName(4000000000u, nb, sizeof nb), "code-4000000000"),
        "an unknown code is named code-N");
  CHECK(verbForCode(0) == AUDIOD_CTRL_TOGGLE && verbForCode(20) == AUDIOD_CTRL_NEXT && verbForCode(21) == AUDIOD_CTRL_PREV &&
        verbForCode(3) == AUDIOD_CTRL_NEXT && verbForCode(4) == AUDIOD_CTRL_PREV,
        "verbs: play/pause TOGGLE, next (20, 3) NEXT, previous (21, 4) PREV");
  bool none = true;
  for (uint32_t c : { 22u, 23u, 24u, 25u, 1u, 2u, 5u, 19u, 26u, 999u }) none = none && verbForCode(c) == 0;
  CHECK(none, "no verb for menu, select, up, down or unknown codes");

  // ---- the swallow decisions ----
  {
    Hook h; h.swallow = true;
    h.send(ev101()); h.send(down(0));
    CHECK(h.dropped == 1 && h.verbs.size() == 1 && h.verbs[0] == AUDIOD_CTRL_TOGGLE && h.lines.empty(),
          "swallow: play/pause 0x400 is dropped and sends TOGGLE, no key line yet (101 passed: %d)", h.passed);
    h.send(up(0));
    CHECK(h.dropped == 2 && h.verbs.size() == 1 && h.lines.size() == 1 && h.lines[0] == "1000 0 play-pause taken" && h.asked == 1,
          "swallow: its 0x800 is dropped too, no second verb, one line '%s', conditions asked once", h.lines.empty() ? "" : h.lines[0].c_str());
  }
  {
    Hook h; h.swallow = true;
    h.send(ev101()); h.send(up(20)); h.send(ev101()); h.send(up(21));
    CHECK(h.dropped == 2 && h.passed == 2 && h.verbs.size() == 2 && h.verbs[0] == AUDIOD_CTRL_NEXT && h.verbs[1] == AUDIOD_CTRL_PREV,
          "swallow: next (0x800, 20) and previous (0x800, 21) are dropped and send NEXT, PREV; both 101s pass");
    CHECK(join(h.lines) == "1000 20 next taken|1000 21 previous taken|", "swallow: one line each: %s", join(h.lines).c_str());
  }
  {
    // B1.16.15: the left key as code 4 (measured) and the right key as code 3 (assumed), one 0x800 event.
    Hook h; h.swallow = true;
    h.send(ev101()); h.send(up(4));
    CHECK(h.dropped == 1 && h.passed == 1 && h.verbs.size() == 1 && h.verbs[0] == AUDIOD_CTRL_PREV && h.asked == 1,
          "swallow: previous as (0x800, 4) is dropped and sends one PREV; the 101 passes");
    h.send(ev101()); h.send(up(3));
    CHECK(h.dropped == 2 && h.passed == 2 && h.verbs.size() == 2 && h.verbs[1] == AUDIOD_CTRL_NEXT && h.asked == 2,
          "swallow: next as (0x800, 3) is dropped and sends one NEXT");
    CHECK(join(h.lines) == "1000 4 previous taken|1000 3 next taken|", "swallow: lines '4 previous taken', '3 next taken': %s",
          join(h.lines).c_str());
  }
  {
    // Should code 4 or 3 ever come as a two-event press: one verb, both events dropped, one line.
    Hook h; h.swallow = true;
    h.send(down(4));
    CHECK(h.dropped == 1 && h.verbs.size() == 1 && h.verbs[0] == AUDIOD_CTRL_PREV && h.lines.empty(),
          "swallow: (0x400, 4) is dropped and sends PREV, no line yet");
    h.send(up(4));
    CHECK(h.dropped == 2 && h.passed == 0 && h.verbs.size() == 1 && h.asked == 1 && join(h.lines) == "1000 4 previous taken|",
          "swallow: its (0x800, 4) is dropped too, no second verb, one line");
    h.send(down(3)); h.send(up(3));
    CHECK(h.dropped == 4 && h.passed == 0 && h.verbs.size() == 2 && h.verbs[1] == AUDIOD_CTRL_NEXT && h.asked == 2,
          "swallow: (0x400, 3) + (0x800, 3): one NEXT, both events dropped");
    CHECK(join(h.lines) == "1000 4 previous taken|1000 3 next taken|", "two-event presses: one line each: %s", join(h.lines).c_str());
  }
  {
    // Without the swallow flag (observe only) codes 3 and 4 pass, one or two events, and the lines say passed.
    Hook h; h.observe = true;
    h.send(up(4)); h.send(up(3)); h.send(down(4)); h.send(up(4)); h.send(down(3)); h.send(up(3));
    CHECK(h.dropped == 0 && h.passed == 6 && h.verbs.empty(), "observe only: codes 3 and 4 pass, no verb");
    CHECK(join(h.lines) == "1000 4 previous passed|1000 3 next passed|1000 4 previous passed|1000 3 next passed|",
          "observe only: '4 previous passed', '3 next passed': %s", join(h.lines).c_str());
    Hook g; g.swallow = true; g.conditions = false;
    g.send(up(4)); g.send(down(3)); g.send(up(3));
    CHECK(g.dropped == 0 && g.passed == 3 && g.verbs.empty() && join(g.lines) == "1000 4 previous passed|1000 3 next passed|",
          "conditions not met: codes 4 and 3 pass, lines say passed");
  }
  {
    Hook h; h.swallow = true;
    for (uint32_t c : { 22u, 23u, 24u, 25u }) { h.send(down(c)); h.send(up(c)); }
    h.send(up(7));
    CHECK(h.dropped == 0 && h.passed == 9 && h.verbs.empty() && h.asked == 0,
          "swallow: menu, select, up, down and an unknown code all pass, no verb, conditions never asked");
    CHECK(join(h.lines) == "1000 22 menu passed|1000 23 select passed|1000 24 up passed|1000 25 down passed|1000 7 code-7 passed|",
          "swallow: one 'passed' line per press, at its 0x800 event");
  }
  {
    Hook h; h.swallow = true; h.conditions = false;  // setting off, no source, or audiod not reachable
    h.send(down(0)); h.send(up(0)); h.send(up(20)); h.send(up(21));
    CHECK(h.dropped == 0 && h.passed == 4 && h.verbs.empty() && h.asked == 3,
          "conditions not met: play/pause, next, previous pass whole, no verb, conditions asked once per press");
    CHECK(join(h.lines) == "1000 0 play-pause passed|1000 20 next passed|1000 21 previous passed|", "conditions not met: lines say passed");
  }
  {
    Hook h; h.swallow = true;
    h.send(down(0)); h.conditions = false; h.send(up(0));
    CHECK(h.dropped == 2 && h.verbs.size() == 1 && h.asked == 1,
          "a press is decided once: conditions failing between its 0x400 and 0x800 still drop the 0x800");
    Hook g; g.swallow = true; g.conditions = false;
    g.send(down(0)); g.conditions = true; g.send(up(0));
    CHECK(g.dropped == 0 && g.passed == 2 && g.verbs.empty(), "and a press passed at its 0x400 passes its 0x800 as well");
  }
  {
    Hook h; h.swallow = true;
    h.send(down(0)); h.swallow = false; h.send(up(0));
    CHECK(h.dropped == 2 && h.lines.size() == 1, "swallow flag removed mid-press: the taken press is still followed to its 0x800");
    h.send(down(0)); h.send(up(0));
    CHECK(h.dropped == 2 && h.passed == 2 && h.lines.size() == 1, "and with no flag after that, the hook passes and writes nothing");
  }
  {
    Hook h; h.observe = true;  // observe only: lines, never a swallow
    h.send(ev101()); h.send(down(0)); h.send(up(0)); h.send(up(20)); h.send(up(21)); h.send(down(22)); h.send(up(22));
    CHECK(h.dropped == 0 && h.passed == 7 && h.verbs.empty() && h.asked == 3,
          "observe only: everything passes, no verb");
    CHECK(join(h.lines) == "1000 0 play-pause passed|1000 20 next passed|1000 21 previous passed|1000 22 menu passed|",
          "observe only: one line per press: %s", join(h.lines).c_str());
  }
  {
    Hook h;  // neither flag
    h.send(ev101()); h.send(down(0)); h.send(up(0)); h.send(up(20));
    CHECK(h.dropped == 0 && h.passed == 4 && h.verbs.empty() && h.lines.empty() && h.asked == 0,
          "neither flag: a pure pass-through that writes nothing");
  }
  {
    Hook h; h.swallow = true;
    h.send(up(0));
    CHECK(h.dropped == 1 && h.verbs.size() == 1 && h.lines.size() == 1, "a play/pause 0x800 without its 0x400 is a press of its own (one verb, one line)");
    h.send(down(0)); h.send(up(22)); h.send(up(0));
    CHECK(h.verbs.size() == 2 && h.lines.size() == 3 && h.lines[1] == "1000 22 menu passed",
          "a 0x800 of another code does not inherit a pending press (menu passed, play/pause still one verb)");
    PressTracker t; KeyEvent k = decodeKey(down(0).b);
    Decision d = t.onKey(k, nullptr, nullptr);
    CHECK(!d.swallow && !d.verb, "no condition callback: never taken");
  }
  {
    // The clean pass of 2026-11-22 (remote-events-pass2.txt, the observe lines 64-88), with swallow on.
    Hook h; h.swallow = true;
    h.send(ev101()); h.send(down(0)); h.send(up(0)); h.send(ev101()); h.send(down(0)); h.send(up(0));
    h.send(ev101()); h.send(up(20)); h.send(ev101()); h.send(up(4));
    h.send(ev101()); h.send(down(22)); h.send(up(22));
    h.send(ev101()); h.send(down(23)); h.send(up(23)); h.send(ev101()); h.send(down(23)); h.send(up(23));
    h.send(ev101()); h.send(down(24)); h.send(up(24)); h.send(ev101()); h.send(down(24)); h.send(up(24));
    // B1.16.15: the code-4 press of that pass (the left key) is previous now, and taken.
    CHECK(h.verbs.size() == 4 && h.verbs[0] == AUDIOD_CTRL_TOGGLE && h.verbs[1] == AUDIOD_CTRL_TOGGLE &&
          h.verbs[2] == AUDIOD_CTRL_NEXT && h.verbs[3] == AUDIOD_CTRL_PREV,
          "the measured pass: verbs TOGGLE, TOGGLE, NEXT, PREV and nothing else");
    CHECK(h.dropped == 6 && h.passed == 19,
          "the measured pass: 6 events dropped (2 x play/pause, next, previous), 19 passed (%d/%d)", h.dropped, h.passed);
    CHECK(h.lines.size() == 9 && h.lines[3] == "1000 4 previous taken",
          "the measured pass: 9 presses, 9 key lines (%zu), the fourth '4 previous taken'", h.lines.size());
  }
  {
    // The later passes of 2026-11-22 (B1.16.13 on the phone): left as (0x800, 4), right as (0x800, 20),
    // down as (0x400, 25) + (0x800, 25); here with swallow on.
    Hook h; h.swallow = true;
    h.send(ev101()); h.send(up(4)); h.send(ev101()); h.send(up(20)); h.send(ev101()); h.send(down(25)); h.send(up(25));
    CHECK(h.verbs.size() == 2 && h.verbs[0] == AUDIOD_CTRL_PREV && h.verbs[1] == AUDIOD_CTRL_NEXT && h.dropped == 2 && h.passed == 5,
          "the later passes: PREV, NEXT; down passes (%d dropped, %d passed)", h.dropped, h.passed);
    CHECK(join(h.lines) == "1000 4 previous taken|1000 20 next taken|1000 25 down passed|", "the later passes: %s",
          join(h.lines).c_str());
  }

  // ---- the key log ----
  char line[96];
  formatKeyLine(line, sizeof line, 1795369452LL, 0, true);
  CHECK(!strcmp(line, "1795369452 0 play-pause taken"), "line format: '%s'", line);
  formatKeyLine(line, sizeof line, 1795369453LL, 99, false);
  CHECK(!strcmp(line, "1795369453 99 code-99 passed"), "line format, unknown code: '%s'", line);

  char dir[] = "/tmp/dockremote-keys-XXXXXX";
  if (!mkdtemp(dir)) { printf("FAIL mkdtemp\n"); return 1; }
  std::string path = std::string(dir) + "/remote-keys.log";
  auto slurp = [&]() { std::string s; FILE* f = fopen(path.c_str(), "r"); if (!f) return s; char b[4096]; size_t n;
                       while ((n = fread(b, 1, sizeof b, f)) > 0) { s.append(b, n); }
                       fclose(f); return s; };
  bool a1 = appendKeyLine(path.c_str(), "1 0 play-pause taken", KEY_LOG_CAP);
  bool a2 = appendKeyLine(path.c_str(), "2 22 menu passed", KEY_LOG_CAP);
  CHECK(a1 && a2 && slurp() == "1 0 play-pause taken\n2 22 menu passed\n", "append: one line per call, in order");
  struct stat st; stat(path.c_str(), &st);
  CHECK((st.st_mode & 0777) == 0644, "the file is created mode 0644 (readable by everyone, mobile included): %o", st.st_mode & 0777);
  { FILE* f = fopen(path.c_str(), "w"); std::string big(KEY_LOG_CAP, 'x'); fwrite(big.data(), 1, big.size(), f); fclose(f); }
  appendKeyLine(path.c_str(), "3 20 next taken", KEY_LOG_CAP);
  CHECK(slurp().size() == (size_t)KEY_LOG_CAP + 16, "a file of exactly 16 KB is appended to");
  appendKeyLine(path.c_str(), "4 21 previous taken", KEY_LOG_CAP);
  CHECK(slurp() == "4 21 previous taken\n", "above 16 KB the file is started afresh before the line");
  { std::string s; for (int i = 0; i < 2000; i++) { formatKeyLine(line, sizeof line, 1795369452LL + i, 23, false);
                                                    appendKeyLine(path.c_str(), line, KEY_LOG_CAP); }
    stat(path.c_str(), &st); }
  CHECK(st.st_size <= KEY_LOG_CAP + 64, "2000 presses: the file stays near 16 KB (%lld bytes)", (long long)st.st_size);
  CHECK(!appendKeyLine((std::string(dir) + "/no/such/dir/x.log").c_str(), "x", KEY_LOG_CAP), "an unwritable path returns false (no crash)");
  unlink(path.c_str()); rmdir(dir);

  printf(fails ? "\n%d FAILURES\n" : "\nall dockremote key checks passed\n", fails);
  return fails ? 1 : 0;
}
