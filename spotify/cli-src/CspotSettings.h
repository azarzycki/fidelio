#pragma once
// B1.14: cspot-ios3's option registry (manifest + settings.conf at startup). Its surfaced options are
// restart-oriented; it reads settings.conf once and writes the manifest at startup.
#include <string>

#include "Settings.h"

namespace cspotcli {

inline void buildRegistry(fidelio::Registry& r) {
  using O = fidelio::Option;
  O o;
  o = {}; o.key="name"; o.flag="--name"; o.type=O::ENUM; o.choices={{"Fidelio","Fidelio"}}; o.def="Fidelio"; o.title="Spotify name"; o.desc="The device name shown in Spotify Connect (restart to change)."; o.group="Spotify"; o.live=false; r.add(o);
  o = {}; o.key="bitrate"; o.flag="--bitrate"; o.type=O::ENUM; o.choices={{"96","96 kbps"},{"160","160 kbps"},{"320","320 kbps"}}; o.def="160"; o.title="Bitrate"; o.desc="Ogg Vorbis bitrate (restart to change)."; o.group="Spotify"; o.live=false; r.add(o);
  o = {}; o.key="initial_volume"; o.flag="--initial-volume"; o.type=O::PERCENT; o.imin=0; o.imax=100; o.def="50"; o.title="Initial volume"; o.desc="Volume reported and played at startup until the first slider move."; o.group="Spotify"; o.live=false; r.add(o);
  o = {}; o.key="volume_curve"; o.flag="--volume-curve"; o.type=O::ENUM; o.choices={{"log","Logarithmic"},{"cubic","Cubic"},{"linear","Linear"}}; o.def="log"; o.title="Volume curve"; o.desc="How the Connect slider maps to the gain (software mode)."; o.group="Spotify"; o.live=false; r.add(o);
}

}  // namespace cspotcli
