#pragma once
// B1.14/B1.15: fidelio-audiod's option registry, declared ONCE here and BOUND to the live ServerOptions
// and Tunables. Each option's read()/write() is the single source of truth: the manifest's effective
// value is read back from the struct the daemon actually uses, and a file value is applied by calling
// write() on that struct. buildRegistry() is called from main (startup) and the Server (live reload)
// with the SAME struct instances, so the manifest always reflects reality.
#include <stdio.h>
#include <stdlib.h>

#include <string>

#include "Server.h"
#include "Settings.h"

namespace audiod {

inline std::string i2s(long v) { char b[32]; snprintf(b, sizeof b, "%ld", v); return b; }
inline std::string b2s(bool v) { return v ? "on" : "off"; }

// Owner of the lock-plugin KeepScreenOn flag (B1.15e): audiod writes <dir>/lockplugin.plist, which the
// SpringBoard lock bundle already live-reloads by mtime. Single owner = audiod (via settings.conf).
inline void writeLockPluginPlist(const std::string& dir, bool keepScreenOn) {
  if (dir.empty()) return;
  std::string path = dir + "/lockplugin.plist", tmp = path + ".tmp";
  FILE* f = fopen(tmp.c_str(), "w");
  if (!f) return;
  fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
             "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
             "<plist version=\"1.0\"><dict>\n\t<key>KeepScreenOn</key>%s\n</dict></plist>\n",
          keepScreenOn ? "<true/>" : "<false/>");
  fclose(f);
  rename(tmp.c_str(), path.c_str());
}

// Build the registry bound to `so`/`t`. `statusDir` is where lockplugin.plist is written.
inline void buildRegistry(fidelio::Registry& r, ServerOptions& so, Tunables& t, const std::string& statusDir) {
  using O = fidelio::Option;
  O o;

  o = {}; o.key="spotify_volume"; o.flag="--spotify-volume"; o.type=O::ENUM;
  o.choices={{"absolute","Absolute (drive the speaker)"},{"software","Software gain"}};
  o.def="absolute"; o.title="Spotify volume"; o.desc="Absolute: the Connect slider sets the device output volume. Software: a gain under it."; o.group="Volume";
  o.read=[&so]{ return so.absoluteVolume ? std::string("absolute") : std::string("software"); };
  o.write=[&so](const std::string& v){ so.absoluteVolume = (v=="absolute"); return true; };
  r.add(o);

  o = {}; o.key="airplay_volume_mode"; o.flag="--airplay-volume-mode"; o.type=O::ENUM;
  o.choices={{"absolute","Absolute (drive the speaker)"},{"software","Software gain"}};
  o.def="absolute"; o.title="AirPlay volume"; o.desc="Absolute: the AirPlay sender's volume sets the device output volume. Software: a gain under it."; o.group="Volume";
  o.read=[&so]{ return so.airplayAbsolute ? std::string("absolute") : std::string("software"); };
  o.write=[&so](const std::string& v){ so.airplayAbsolute = (v=="absolute"); return true; };
  r.add(o);

  o = {}; o.key="max_volume"; o.flag="--max-volume"; o.type=O::PERCENT; o.imin=0; o.imax=100;
  o.def="100"; o.title="Maximum volume"; o.desc="Ceiling the hardware level is never driven above."; o.group="Volume";
  o.read=[&so]{ return i2s(so.maxVolumePct); };
  o.write=[&so](const std::string& v){ so.maxVolumePct = (int)strtol(v.c_str(),nullptr,10); return true; };
  r.add(o);

  o = {}; o.key="hw_volume_curve"; o.flag="--hw-volume-curve"; o.type=O::ENUM;
  o.choices={{"linear","Linear"},{"log","Logarithmic"},{"cubic","Cubic"}};
  o.def="linear"; o.title="Volume curve"; o.desc="How a slider position maps to the hardware level in absolute mode."; o.group="Volume"; o.advanced=true;
  o.read=[&so]{ return so.hwCurve==1 ? std::string("log") : so.hwCurve==2 ? std::string("cubic") : std::string("linear"); };
  o.write=[&so](const std::string& v){ so.hwCurve = (v=="log")?1:(v=="cubic")?2:0; return true; };
  r.add(o);

  o = {}; o.key="crossfade_ms"; o.flag="--crossfade-ms"; o.type=O::INT; o.imin=0; o.imax=200; o.unit="ms";
  o.def="30"; o.title="Crossfade"; o.desc="Length of the ramp when the active source switches (0 = hard cut)."; o.group="Source switching";
  o.read=[&so]{ return i2s(so.crossfadeMs); };
  o.write=[&so](const std::string& v){ so.crossfadeMs = (unsigned)strtol(v.c_str(),nullptr,10); return true; };
  r.add(o);

  o = {}; o.key="preempt_pause"; o.flag="--preempt-pause"; o.type=O::BOOL;
  o.def="on"; o.title="Pause losing source"; o.desc="Tell a source that loses the output to pause, so it cannot roll on and steal it back."; o.group="Source switching";
  o.read=[&so]{ return b2s(so.preemptPause); };
  o.write=[&so](const std::string& v){ so.preemptPause = (v=="on"); return true; };
  r.add(o);

  o = {}; o.key="preempt_delay_ms"; o.flag="--preempt-delay-ms"; o.type=O::INT; o.imin=0; o.imax=3000; o.unit="ms";
  o.def="500"; o.title="Preempt delay"; o.desc="Wait this long before pausing the loser, so a brief flap does not pause the active source."; o.group="Source switching"; o.advanced=true;
  o.read=[&so]{ return i2s(so.preemptDelayMs); };
  o.write=[&so](const std::string& v){ so.preemptDelayMs = (unsigned)strtol(v.c_str(),nullptr,10); return true; };
  r.add(o);

  o = {}; o.key="wake_on_play"; o.flag="--wake-on-play"; o.type=O::BOOL;
  o.def="on"; o.title="Wake on play"; o.desc="Wake the screen and bring NowPlaying forward when playback starts from nothing."; o.group="Display";
  o.read=[&so]{ return b2s(so.wakeOnPlay); };
  o.write=[&so](const std::string& v){ so.wakeOnPlay = (v=="on"); return true; };
  r.add(o);

  // B1.16.10: what the wake does, chosen per device in the NowPlaying Settings tab. The lock plugin
  // stays the backstop either way.
  o = {}; o.key="wake_mode"; o.flag="--wake-mode"; o.type=O::ENUM;
  o.choices={{"launch","Open NowPlaying"},{"screen","Screen only"}};
  o.def="launch"; o.title="Wake shows";
  o.desc="What Wake on play does when playback starts. Open NowPlaying: bring the app forward (a locked iPad unlocks first when Unlock on play is on; the iPhone opens it over the lock screen). Screen only: just light the screen, so a locked device shows the lock-screen plugin, nothing is opened or unlocked.";
  o.group="Display";
  o.read=[&so]{ return so.wakeLaunch ? std::string("launch") : std::string("screen"); };
  o.write=[&so](const std::string& v){ so.wakeLaunch = (v != "screen"); return true; };
  r.add(o);

  // B1.16.4: unlock a locked device when playback starts (needs wake on play).
  o = {}; o.key="unlock_on_play"; o.flag="--unlock-on-play"; o.type=O::BOOL;
  o.def="on"; o.title="Unlock on play"; o.desc="When playback starts and the device is locked, unlock it and show NowPlaying (works only without a passcode; needs Wake on play with Wake shows = Open NowPlaying; the iPhone opens the app over the lock screen without it). Off: the device stays on the lock screen.";
  o.group="Display";
  o.read=[&so]{ return b2s(so.unlockOnPlay); };
  o.write=[&so](const std::string& v){ so.unlockOnPlay = (v=="on"); return true; };
  r.add(o);

  o = {}; o.key="lock_plugin"; o.flag="--lock-plugin"; o.type=O::BOOL;
  o.def="off"; o.title="Lock-screen plugin"; o.desc="Show the track on the lock screen, the backstop whenever NowPlaying is not in front (needs Fidelio.lockbundle on the iPad, Fidelio.bundle on the iPhone)."; o.group="Display";
  o.read=[&so]{ return so.lockPlugin.empty() ? std::string("off") : std::string("on"); };
  o.write=[&so](const std::string& v){ so.lockPlugin = (v=="on") ? std::string("Fidelio") : std::string(); return true; };
  r.add(o);

  // B1.15e: KeepScreenOn for the lock bundle, owned here (audiod writes lockplugin.plist).
  o = {}; o.key="keep_screen_on"; o.flag="--keep-screen-on"; o.type=O::BOOL;
  o.def="off"; o.title="Keep screen on"; o.desc="On the lock screen, keep the display lit while something is playing (done by the lock-screen plugin, both devices)."; o.group="Display";
  o.read=[&so]{ return b2s(so.keepScreenOn); };
  o.write=[&so,statusDir](const std::string& v){ so.keepScreenOn = (v=="on"); writeLockPluginPlist(statusDir, so.keepScreenOn); return true; };
  r.add(o);

  // B1.16: the dock remote switch. audiod carries it; the DockRemote tweak reads it from settings.conf.
  o = {}; o.key="dock_remote"; o.flag="--dock-remote"; o.type=O::BOOL;
  o.def="off"; o.title="Dock remote controls"; o.desc="Let the dock's infrared/accessory remote (play/next/previous) drive the active source instead of the built-in music app. Needs the DockRemote tweak installed (iPhone)."; o.group="Controls";
  o.read=[&so]{ return b2s(so.dockRemote); };
  o.write=[&so](const std::string& v){ so.dockRemote = (v=="on"); return true; };
  r.add(o);

  // Advanced: arbiter timing (bound to Tunables)
  o = {}; o.key="takeover_ms"; o.flag="--takeover-ms"; o.type=O::INT; o.imin=100; o.imax=2000; o.unit="ms";
  o.def="400"; o.title="Takeover time"; o.desc="How long a louder source must play before it takes the output."; o.group="Advanced"; o.advanced=true;
  o.read=[&t]{ return i2s((long)t.takeoverMs); };
  o.write=[&t](const std::string& v){ t.takeoverMs = strtoull(v.c_str(),nullptr,10); return true; };
  r.add(o);

  o = {}; o.key="hold_ms"; o.flag="--hold-ms"; o.type=O::INT; o.imin=0; o.imax=10000; o.unit="ms";
  o.def="1500"; o.title="Hold time"; o.desc="Minimum time a source keeps the output after a switch."; o.group="Advanced"; o.advanced=true;
  o.read=[&t]{ return i2s((long)t.holdMs); };
  o.write=[&t](const std::string& v){ t.holdMs = strtoull(v.c_str(),nullptr,10); return true; };
  r.add(o);

  o = {}; o.key="release_ms"; o.flag="--release-ms"; o.type=O::INT; o.imin=200; o.imax=10000; o.unit="ms";
  o.def="1500"; o.title="Release time"; o.desc="No audio at all for this long releases the source."; o.group="Advanced"; o.advanced=true;
  o.read=[&t]{ return i2s((long)t.releaseMs); };
  o.write=[&t](const std::string& v){ t.releaseMs = strtoull(v.c_str(),nullptr,10); return true; };
  r.add(o);

  o = {}; o.key="producing_gap_ms"; o.flag="--producing-gap-ms"; o.type=O::INT; o.imin=100; o.imax=5000; o.unit="ms";
  o.def="1000"; o.title="Producing gap"; o.desc="A gap shorter than this is tolerated as a hiccup, not a stop."; o.group="Advanced"; o.advanced=true;
  o.read=[&t]{ return i2s((long)t.producingGapMs); };
  o.write=[&t](const std::string& v){ t.producingGapMs = strtoull(v.c_str(),nullptr,10); return true; };
  r.add(o);

  o = {}; o.key="lead_ms"; o.flag="--lead-ms"; o.type=O::INT; o.imin=200; o.imax=2000; o.unit="ms";
  o.def="750"; o.title="Lead"; o.desc="How far ahead of real time the producers stream (restart to change)."; o.group="Advanced"; o.advanced=true; o.live=false;
  o.read=[&so]{ return i2s(so.leadMs); };
  o.write=[&so](const std::string& v){ so.leadMs = (unsigned)strtol(v.c_str(),nullptr,10); return true; };
  r.add(o);
}

}  // namespace audiod
