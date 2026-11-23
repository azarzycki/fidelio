// B1.14/B1.15: the settings Registry. B1.15 makes each option BOUND to a live struct: read()/write()
// are the single source of truth, the manifest reads back what the struct holds, a file value is
// applied by calling write(), command-line detection covers every flag spelling (short and long), and
// selfCheck() catches a registry default that drifts from the program's or a file value not applied.
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "Settings.h"
using namespace dockreceiver;

static int fails = 0;
#define CHECK(c, m) do { if (c) printf("ok   %s\n", m); else { printf("FAIL %s\n", m); fails++; } } while (0)

// A stand-in for a daemon's options struct. The registry binds to THIS, exactly as the real daemons do.
struct Cfg {
  int maxVol = 100;
  bool wake = true;
  int mode = 0;            // 0 = absolute, 1 = software
  std::string name = "iPhone Fidelio";
  int xfade = 30;
};

static std::string i2s(long v) { char b[32]; snprintf(b, sizeof b, "%ld", v); return b; }

// Build a registry bound to cfg. `nameDef`/`maxDef` let a test introduce a deliberate default drift.
static Registry build(Cfg& c, const char* maxDef = "100", const char* nameDef = "iPhone Fidelio") {
  Registry r("audiod", "testhash");
  Option m; m.key="max_volume"; m.flag="--max-volume"; m.flags={"--max-volume","-m"}; m.type=Option::PERCENT; m.imin=0; m.imax=100; m.def=maxDef;
  m.title="Maximum volume"; m.desc="Ceiling."; m.group="Volume";
  m.read=[&c]{ return i2s(c.maxVol); }; m.write=[&c](const std::string& v){ c.maxVol=(int)strtol(v.c_str(),0,10); return true; };
  r.add(m);
  Option w; w.key="wake_on_play"; w.flag="--wake-on-play"; w.type=Option::BOOL; w.def="on";
  w.title="Wake"; w.desc="Wake the screen."; w.group="Display";
  w.read=[&c]{ return c.wake ? std::string("on") : std::string("off"); }; w.write=[&c](const std::string& v){ c.wake=(v=="on"); return true; };
  r.add(w);
  Option e; e.key="airplay_volume_mode"; e.flag="--airplay-volume-mode"; e.type=Option::ENUM;
  e.choices={{"absolute","Absolute"},{"software","Software"}}; e.def="absolute"; e.title="AirPlay volume"; e.desc="."; e.group="Volume";
  e.read=[&c]{ return c.mode==0 ? std::string("absolute") : std::string("software"); }; e.write=[&c](const std::string& v){ c.mode=(v=="absolute")?0:1; return true; };
  r.add(e);
  Option n; n.key="name"; n.flag="--name"; n.flags={"--name","-n"}; n.type=Option::STRING; n.maxlen=63; n.def=nameDef;
  n.title="Name"; n.desc="Device name."; n.group="Spotify"; n.live=false;
  n.read=[&c]{ return c.name; }; n.write=[&c](const std::string& v){ c.name=v; return true; };
  r.add(n);
  Option x; x.key="crossfade_ms"; x.flag="--crossfade-ms"; x.type=Option::INT; x.imin=0; x.imax=200; x.unit="ms"; x.def="30";
  x.title="Crossfade"; x.desc="."; x.group="Advanced"; x.advanced=true;
  x.read=[&c]{ return i2s(c.xfade); }; x.write=[&c](const std::string& v){ c.xfade=(int)strtol(v.c_str(),0,10); return true; };
  r.add(x);
  return r;
}

int main() {
  const char* dir = "/tmp/audiod-settings-test";
  char cmd[128]; snprintf(cmd, sizeof cmd, "mkdir -p %s", dir); system(cmd);
  std::string conf = std::string(dir) + "/settings.conf";

  // 1. defaults read back from the bound struct
  { Cfg c; Registry r = build(c); r.refreshValues();
    CHECK(r.get("max_volume")=="100" && r.getBool("wake_on_play") && r.get("airplay_volume_mode")=="absolute" && r.get("name")=="iPhone Fidelio", "defaults read back from the live struct"); }

  // 2. settings.conf is APPLIED to the struct (effect), not just a parallel copy
  { Cfg c; Registry r = build(c);
    FILE* f = fopen(conf.c_str(), "w");
    fputs("audiod.max_volume=80\naudiod.wake_on_play=off\naudiod.airplay_volume_mode=software\naudiod.name=Living Room\n# a comment\nspotify.bitrate=320\n", f); fclose(f);
    r.loadFile(conf);
    CHECK(c.maxVol==80 && !c.wake && c.mode==1 && c.name=="Living Room", "settings.conf values reach the live struct (effect), another daemon's line ignored");
    r.refreshValues();
    CHECK(r.get("max_volume")=="80", "manifest value tracks the struct after a file apply"); }

  // 3. command line (LONG) beats the file, marked locked; short alias is detected too
  { Cfg c; Registry r = build(c);
    char a0[]="audiod", a1[]="--max-volume", a2[]="50"; char* argv[]={a0,a1,a2};
    c.maxVol = 50;  // the hand parser set this; the registry only marks locked
    r.markCommandLine(3, argv);
    FILE* f=fopen(conf.c_str(),"w"); fputs("audiod.max_volume=80\n", f); fclose(f);
    r.loadFile(conf); r.refreshValues();
    CHECK(c.maxVol==50 && r.get("max_volume")=="50", "command line beats settings.conf (file not applied to a locked option)");
    const Option* o=0; for (auto& x:r.options()) if (x.key=="max_volume") o=&x;
    CHECK(o && o->locked && o->origin=="command line", "the long flag marks the option locked"); }
  { Cfg c; Registry r = build(c);
    char a0[]="audiod", a1[]="-m", a2[]="70"; char* argv[]={a0,a1,a2};
    c.maxVol = 70;
    r.markCommandLine(3, argv);
    FILE* f=fopen(conf.c_str(),"w"); fputs("audiod.max_volume=80\n", f); fclose(f);
    r.loadFile(conf); r.refreshValues();
    const Option* o=0; for (auto& x:r.options()) if (x.key=="max_volume") o=&x;
    CHECK(c.maxVol==70 && o && o->locked, "the SHORT alias (-m) also marks the option locked and the file is ignored"); }
  { Cfg c; Registry r = build(c);
    char a0[]="cspot", a1[]="-n", a2[]="iPad Fidelio"; char* argv[]={a0,a1,a2};
    c.name = "iPad Fidelio";
    r.markCommandLine(3, argv); r.refreshValues();
    const Option* o=0; for (auto& x:r.options()) if (x.key=="name") o=&x;
    CHECK(o && o->locked && o->origin=="command line" && r.get("name")=="iPad Fidelio", "a STRING name set by the short -n is locked and shown as the real value"); }

  // 4. garbage / unknown / out-of-range ignored, the struct stays usable
  { Cfg c; Registry r = build(c);
    FILE* f=fopen(conf.c_str(),"w");
    fputs("garbage line no equals\naudiod.nonexistent=1\naudiod.max_volume=99999\naudiod.max_volume=75\naudiod.wake_on_play=banana\n", f); fclose(f);
    r.loadFile(conf);
    CHECK(c.maxVol==75 && c.wake, "garbage/unknown/out-of-range ignored; valid value applied, bad bool kept default"); }

  // 5. STRING validation: too long and control chars rejected, a sane name accepted
  { Cfg c; Registry r = build(c);
    std::string longName(80, 'x');
    FILE* f=fopen(conf.c_str(),"w"); fprintf(f, "audiod.name=%s\n", longName.c_str()); fclose(f);
    r.loadFile(conf);
    CHECK(c.name=="iPhone Fidelio", "an over-length name (>63) is rejected, the default kept");
    f=fopen(conf.c_str(),"w"); fputs("audiod.name=Kitchen Dock\n", f); fclose(f);
    r.loadFile(conf);
    CHECK(c.name=="Kitchen Dock", "a normal name with a space is accepted and applied"); }

  // 6. live reload applies to the struct
  { Cfg c; Registry r = build(c);
    FILE* f=fopen(conf.c_str(),"w"); fputs("audiod.max_volume=60\n", f); fclose(f);
    r.reloadIfChanged(conf);
    CHECK(c.maxVol==60, "reloadIfChanged applies the initial file to the struct");
    CHECK(!r.reloadIfChanged(conf), "no change -> reloadIfChanged returns false");
    sleep(1);
    f=fopen(conf.c_str(),"w"); fputs("audiod.max_volume=40\n", f); fclose(f);
    CHECK(r.reloadIfChanged(conf) && c.maxVol==40, "a changed file is reloaded and the struct updates"); }

  // 7. selfCheck passes when consistent, FAILs when the registry default drifts from the program's
  { Cfg c; Registry r = build(c); r.loadFile("/does/not/exist");
    CHECK(r.selfCheck(), "selfCheck passes when every default matches the struct"); }
  { Cfg c; Registry r = build(c, "90");  // declare max_volume default 90 but the struct is 100
    CHECK(!r.selfCheck(), "selfCheck FAILS when a registry default (90) differs from the program's (100)"); }
  { Cfg c; Registry r = build(c);        // a file value the write() refuses would not be applied
    FILE* f=fopen(conf.c_str(),"w"); fputs("audiod.max_volume=55\n", f); fclose(f);
    r.loadFile(conf);
    CHECK(r.selfCheck(), "selfCheck passes when a file value really reached the struct"); }

  // 8. manifest is a loadable plist with the right fields incl. the STRING type + maxlen
  { Cfg c; Registry r = build(c);
    char a0[]="audiod", a1[]="--wake-on-play", a2[]="off"; char* argv[]={a0,a1,a2};
    c.wake=false; r.markCommandLine(3, argv); r.refreshValues();
    CHECK(r.writeManifest(dir), "manifest written");
    std::string mf = std::string(dir)+"/settings-audiod.manifest.plist";
    FILE* f=fopen(mf.c_str(),"r"); std::string s; char buf[4096]; size_t n;
    while ((n=fread(buf,1,sizeof buf,f))>0) s.append(buf,n); fclose(f);
    CHECK(s.find("<key>daemon</key><string>audiod</string>")!=std::string::npos, "manifest names the daemon");
    CHECK(s.find("<key>value</key><string>off</string>")!=std::string::npos, "manifest has the effective value read back from the struct");
    CHECK(s.find("<key>locked</key><true/>")!=std::string::npos, "manifest marks the command-line option locked");
    CHECK(s.find("<string>absolute</string>")!=std::string::npos && s.find("<key>choices</key>")!=std::string::npos, "manifest has enum choices");
    CHECK(s.find("<key>unit</key><string>ms</string>")!=std::string::npos, "manifest has the integer unit");
    CHECK(s.find("<key>type</key><string>string</string>")!=std::string::npos && s.find("<key>maxlen</key>")!=std::string::npos, "manifest has the STRING type and maxlen"); }

  printf(fails ? "\n%d FAILURES\n" : "\nall settings checks passed\n", fails);
  return fails ? 1 : 0;
}
