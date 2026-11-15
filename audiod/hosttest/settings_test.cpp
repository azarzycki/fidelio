// B1.14: the settings Registry — command line > settings.conf > default, the locked marker, live
// reload on file change, garbage/unknown-line tolerance, and a manifest the app can parse.
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "Settings.h"
using namespace fidelio;

static int fails = 0;
#define CHECK(c, m) do { if (c) printf("ok   %s\n", m); else { printf("FAIL %s\n", m); fails++; } } while (0)

static Registry build() {
  Registry r("audiod", "testhash");
  Option vol; vol.key = "airplay_volume_mode"; vol.flag = "--airplay-volume-mode"; vol.type = Option::ENUM;
  vol.choices = {{"absolute", "Absolute"}, {"software", "Software"}}; vol.def = "absolute";
  vol.title = "AirPlay volume"; vol.desc = "The AirPlay sender drives the device level."; vol.group = "Volume";
  r.add(vol);
  Option mx; mx.key = "max_volume"; mx.flag = "--max-volume"; mx.type = Option::PERCENT; mx.imin = 0; mx.imax = 100; mx.def = "100";
  mx.title = "Maximum volume"; mx.desc = "Ceiling for the hardware level."; mx.group = "Volume"; r.add(mx);
  Option wk; wk.key = "wake_on_play"; wk.flag = "--wake-on-play"; wk.type = Option::BOOL; wk.def = "on";
  wk.title = "Wake on play"; wk.desc = "Wake the screen when playback starts."; wk.group = "Display"; r.add(wk);
  Option xf; xf.key = "crossfade_ms"; xf.flag = "--crossfade-ms"; xf.type = Option::INT; xf.imin = 0; xf.imax = 200; xf.unit = "ms"; xf.def = "30";
  xf.title = "Crossfade"; xf.desc = "Source-switch ramp."; xf.group = "Advanced"; xf.advanced = true; r.add(xf);
  return r;
}

int main() {
  const char* dir = "/tmp/audiod-settings-test";
  char cmd[128]; snprintf(cmd, sizeof cmd, "mkdir -p %s", dir); system(cmd);
  std::string conf = std::string(dir) + "/settings.conf";

  // 1. defaults
  { Registry r = build();
    CHECK(r.get("max_volume") == "100" && r.getBool("wake_on_play") && r.get("airplay_volume_mode") == "absolute", "defaults as declared"); }

  // 2. settings.conf overrides the default; precedence command line > file
  { Registry r = build();
    FILE* f = fopen(conf.c_str(), "w");
    fputs("audiod.max_volume=80\naudiod.wake_on_play=off\naudiod.airplay_volume_mode=software\n# a comment\nspotify.bitrate=320\n", f); fclose(f);
    r.loadFile(conf);
    CHECK(r.getInt("max_volume") == 80 && !r.getBool("wake_on_play") && r.get("airplay_volume_mode") == "software", "settings.conf overrides defaults; another daemon's line ignored");
    const Option* o = nullptr; for (auto& x : r.options()) if (x.key == "max_volume") o = &x;
    CHECK(o && o->origin == "file", "origin marked as file"); }

  // 3. command line beats the file, and is locked
  { Registry r = build();
    char a0[] = "audiod", a1[] = "--max-volume", a2[] = "50"; char* argv[] = {a0, a1, a2};
    r.parseArgv(3, argv);
    FILE* f = fopen(conf.c_str(), "w"); fputs("audiod.max_volume=80\n", f); fclose(f);
    r.loadFile(conf);
    CHECK(r.getInt("max_volume") == 50, "command line beats settings.conf");
    const Option* o = nullptr; for (auto& x : r.options()) if (x.key == "max_volume") o = &x;
    CHECK(o && o->locked && o->origin == "command line", "command-line option is locked"); }

  // 4. garbage / unknown / out-of-range ignored, daemon still usable
  { Registry r = build();
    FILE* f = fopen(conf.c_str(), "w");
    fputs("garbage line no equals\naudiod.nonexistent=1\naudiod.max_volume=99999\naudiod.max_volume=75\naudiod.wake_on_play=banana\n", f); fclose(f);
    r.loadFile(conf);
    CHECK(r.getInt("max_volume") == 75 && r.getBool("wake_on_play"), "garbage/unknown/out-of-range ignored; valid value applied, bool kept default"); }

  // 5. live reload on mtime change
  { Registry r = build();
    FILE* f = fopen(conf.c_str(), "w"); fputs("audiod.max_volume=60\n", f); fclose(f);
    r.reloadIfChanged(conf);
    CHECK(r.getInt("max_volume") == 60, "reloadIfChanged picks up the initial file");
    CHECK(!r.reloadIfChanged(conf), "no change -> reloadIfChanged returns false");
    sleep(1);  // mtime has 1 s resolution
    f = fopen(conf.c_str(), "w"); fputs("audiod.max_volume=40\n", f); fclose(f);
    bool ch = r.reloadIfChanged(conf);
    CHECK(ch && r.getInt("max_volume") == 40, "a changed file is reloaded and the value updates"); }

  // 6. manifest is a loadable plist with the right fields
  { Registry r = build();
    char a0[] = "audiod", a1[] = "--wake-on-play", a2[] = "off"; char* argv[] = {a0, a1, a2};
    r.parseArgv(3, argv);
    CHECK(r.writeManifest(dir), "manifest written");
    std::string mf = std::string(dir) + "/settings-audiod.manifest.plist";
    FILE* f = fopen(mf.c_str(), "r"); std::string c; char buf[4096]; size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) c.append(buf, n); fclose(f);
    CHECK(c.find("<key>daemon</key><string>audiod</string>") != std::string::npos, "manifest names the daemon");
    CHECK(c.find("<key>value</key><string>off</string>") != std::string::npos, "manifest has the effective value");
    CHECK(c.find("<key>locked</key><true/>") != std::string::npos, "manifest marks the command-line option locked");
    CHECK(c.find("<string>absolute</string>") != std::string::npos && c.find("<key>choices</key>") != std::string::npos, "manifest has enum choices");
    CHECK(c.find("<key>unit</key><string>ms</string>") != std::string::npos, "manifest has the integer unit"); }

  printf(fails ? "\n%d FAILURES\n" : "\nall settings checks passed\n", fails);
  return fails ? 1 : 0;
}
