#pragma once
// B1.14: fidelio-audiod's option registry, declared ONCE here. main builds it, parses the command
// line and settings.conf through it, writes the manifest, and re-applies it live on a change. The
// command-line flags below match the hand parser in main.cpp; a startup check asserts they do not
// drift (every registry flag must appear in --help).
#include <string>

#include "Server.h"
#include "Settings.h"

namespace audiod {

inline void buildRegistry(fidelio::Registry& r) {
  using O = fidelio::Option;
  auto enumc = [](std::vector<fidelio::Choice> c) { return c; };
  O o;
  o = {}; o.key="spotify_volume"; o.flag="--spotify-volume"; o.type=O::ENUM; o.choices=enumc({{"absolute","Absolute (drive the speaker)"},{"software","Software gain"}}); o.def="absolute"; o.title="Spotify volume"; o.desc="Absolute: the Connect slider sets the device output volume. Software: a gain under it."; o.group="Volume"; r.add(o);
  o = {}; o.key="airplay_volume_mode"; o.flag="--airplay-volume-mode"; o.type=O::ENUM; o.choices=enumc({{"absolute","Absolute (drive the speaker)"},{"software","Software gain"}}); o.def="absolute"; o.title="AirPlay volume"; o.desc="Absolute: the AirPlay sender's volume sets the device output volume. Software: a gain under it."; o.group="Volume"; r.add(o);
  o = {}; o.key="max_volume"; o.flag="--max-volume"; o.type=O::PERCENT; o.imin=0; o.imax=100; o.def="100"; o.title="Maximum volume"; o.desc="Ceiling the hardware level is never driven above."; o.group="Volume"; r.add(o);
  o = {}; o.key="hw_volume_curve"; o.flag="--hw-volume-curve"; o.type=O::ENUM; o.choices=enumc({{"linear","Linear"},{"log","Logarithmic"},{"cubic","Cubic"}}); o.def="linear"; o.title="Volume curve"; o.desc="How a slider position maps to the hardware level in absolute mode."; o.group="Volume"; o.advanced=true; r.add(o);
  o = {}; o.key="crossfade_ms"; o.flag="--crossfade-ms"; o.type=O::INT; o.imin=0; o.imax=200; o.unit="ms"; o.def="30"; o.title="Crossfade"; o.desc="Length of the ramp when the active source switches (0 = hard cut)."; o.group="Source switching"; r.add(o);
  o = {}; o.key="preempt_pause"; o.flag="--preempt-pause"; o.type=O::BOOL; o.def="on"; o.title="Pause the losing source"; o.desc="Tell a source that loses the output to pause, so it cannot roll on and steal it back."; o.group="Source switching"; r.add(o);
  o = {}; o.key="preempt_delay_ms"; o.flag="--preempt-delay-ms"; o.type=O::INT; o.imin=0; o.imax=3000; o.unit="ms"; o.def="500"; o.title="Preempt delay"; o.desc="Wait this long before pausing the loser, so a brief flap does not pause the active source."; o.group="Source switching"; o.advanced=true; r.add(o);
  o = {}; o.key="wake_on_play"; o.flag="--wake-on-play"; o.type=O::BOOL; o.def="on"; o.title="Wake on play"; o.desc="Wake the screen and bring NowPlaying forward when playback starts from nothing."; o.group="Display"; r.add(o);
  o = {}; o.key="lock_plugin"; o.flag="--lock-plugin"; o.type=O::BOOL; o.def="off"; o.title="Lock-screen plugin"; o.desc="Show the track on the lock screen (iPad; needs Fidelio.lockbundle installed)."; o.group="Display"; r.add(o);
  // Advanced: arbiter timing
  o = {}; o.key="takeover_ms"; o.flag="--takeover-ms"; o.type=O::INT; o.imin=100; o.imax=2000; o.unit="ms"; o.def="400"; o.title="Takeover time"; o.desc="How long a louder source must play before it takes the output."; o.group="Advanced"; o.advanced=true; r.add(o);
  o = {}; o.key="hold_ms"; o.flag="--hold-ms"; o.type=O::INT; o.imin=0; o.imax=10000; o.unit="ms"; o.def="1500"; o.title="Hold time"; o.desc="Minimum time a source keeps the output after a switch."; o.group="Advanced"; o.advanced=true; r.add(o);
  o = {}; o.key="release_ms"; o.flag="--release-ms"; o.type=O::INT; o.imin=200; o.imax=10000; o.unit="ms"; o.def="1500"; o.title="Release time"; o.desc="No audio at all for this long releases the source."; o.group="Advanced"; o.advanced=true; r.add(o);
  o = {}; o.key="producing_gap_ms"; o.flag="--producing-gap-ms"; o.type=O::INT; o.imin=100; o.imax=5000; o.unit="ms"; o.def="1000"; o.title="Producing gap"; o.desc="A gap shorter than this is tolerated as a hiccup, not a stop."; o.group="Advanced"; o.advanced=true; r.add(o);
  o = {}; o.key="lead_ms"; o.flag="--lead-ms"; o.type=O::INT; o.imin=200; o.imax=2000; o.unit="ms"; o.def="750"; o.title="Lead"; o.desc="How far ahead of real time the producers stream (restart to change)."; o.group="Advanced"; o.advanced=true; o.live=false; r.add(o);
}

// Apply the registry's non-default values to the running config (live subset). Called at startup and
// on every settings.conf change. Only options explicitly set (file or command line) are applied, so
// env-set tunables and struct defaults are preserved.
inline void applyRegistry(const fidelio::Registry& r, ServerOptions& so, Tunables& t) {
  for (const fidelio::Option& o : r.options()) {
    if (o.origin != "file") continue;  // B1.14: the hand parser owns the command line; the registry applies settings.conf
    const std::string& k = o.key; const std::string& v = o.value;
    if (k == "spotify_volume") so.absoluteVolume = (v == "absolute");
    else if (k == "airplay_volume_mode") so.airplayAbsolute = (v == "absolute");
    else if (k == "max_volume") so.maxVolumePct = (int)strtol(v.c_str(), nullptr, 10);
    else if (k == "hw_volume_curve") so.hwCurve = (v == "log") ? 1 : (v == "cubic") ? 2 : 0;
    else if (k == "crossfade_ms") so.crossfadeMs = (unsigned)strtol(v.c_str(), nullptr, 10);
    else if (k == "preempt_pause") so.preemptPause = (v == "on");
    else if (k == "preempt_delay_ms") so.preemptDelayMs = (unsigned)strtol(v.c_str(), nullptr, 10);
    else if (k == "wake_on_play") so.wakeOnPlay = (v == "on");
    else if (k == "lock_plugin") so.lockPlugin = (v == "on") ? "Fidelio" : "";
    else if (k == "takeover_ms") t.takeoverMs = strtoull(v.c_str(), nullptr, 10);
    else if (k == "hold_ms") t.holdMs = strtoull(v.c_str(), nullptr, 10);
    else if (k == "release_ms") t.releaseMs = strtoull(v.c_str(), nullptr, 10);
    else if (k == "producing_gap_ms") t.producingGapMs = strtoull(v.c_str(), nullptr, 10);
    else if (k == "lead_ms") so.leadMs = (unsigned)strtol(v.c_str(), nullptr, 10);
  }
}

}  // namespace audiod
