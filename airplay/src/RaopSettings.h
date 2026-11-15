#pragma once
// B1.14: fidelio-raop's option registry (manifest + settings.conf at startup). raop's surfaced options
// are mostly restart-oriented, so it reads settings.conf once at startup and writes the manifest; the
// app restarts raop (killall + launchd KeepAlive) to change a restart option.
#include <string>

#include "Settings.h"

namespace raop {

inline void buildRegistry(fidelio::Registry& r) {
  using O = fidelio::Option;
  O o;
  o = {}; o.key="name"; o.flag="--name"; o.type=O::ENUM; o.choices={{"Fidelio (AirPlay)","Fidelio (AirPlay)"}}; o.def="Fidelio (AirPlay)"; o.title="AirPlay name"; o.desc="The name shown in the AirPlay picker (restart to change)."; o.group="AirPlay"; o.live=false; r.add(o);
  o = {}; o.key="airplay_volume"; o.flag="--airplay-volume"; o.type=O::ENUM; o.choices={{"auto","Auto"},{"honor","Honor (incl. mute)"},{"ignore","Ignore (always unity)"}}; o.def="auto"; o.title="Sender volume handling"; o.desc="Auto: honour -30..0 dB, treat -144 as 'no remote volume'. Honor: apply all incl. mute. Ignore: always unity."; o.group="AirPlay"; o.live=false; r.add(o);
  o = {}; o.key="airplay_preempt"; o.flag="--airplay-preempt"; o.type=O::ENUM; o.choices={{"pause-then-disconnect","Pause, then disconnect"},{"pause-only","Pause only"},{"disconnect","Disconnect"},{"none","Keep draining"}}; o.def="pause-then-disconnect"; o.title="On losing the output"; o.desc="What to do with the AirPlay sender when another source takes over."; o.group="AirPlay"; o.live=false; r.add(o);
  o = {}; o.key="preempt_verify_ms"; o.flag="--preempt-verify-ms"; o.type=O::INT; o.imin=500; o.imax=10000; o.unit="ms"; o.def="3000"; o.title="Preempt verify window"; o.desc="How long to watch the stream for the sender to pause before disconnecting it."; o.group="AirPlay"; o.advanced=true; o.live=false; r.add(o);
  o = {}; o.key="preempt_cooldown_ms"; o.flag="--preempt-cooldown-ms"; o.type=O::INT; o.imin=0; o.imax=60000; o.unit="ms"; o.def="10000"; o.title="Preempt cool-down"; o.desc="After a forced disconnect, refuse the same sender for this long."; o.group="AirPlay"; o.advanced=true; o.live=false; r.add(o);
  o = {}; o.key="latency_ms"; o.flag="--latency-ms"; o.type=O::INT; o.imin=200; o.imax=3800; o.unit="ms"; o.def="1200"; o.title="Jitter buffer"; o.desc="Buffer depth before playback starts (restart to change)."; o.group="Advanced"; o.advanced=true; o.live=false; r.add(o);
}

}  // namespace raop
