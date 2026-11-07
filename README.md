# Fidelio

Turning a jailbroken 2007 iPhone (iPhone1,1 · ARMv6/ARM11 · iOS 3.1.3), permanently
docked in a Philips Fidelio DS9000, into a headless audio-receiver appliance.

## Components

| Dir | What |
|-----|------|
| `spotify/`    | cspot Spotify Connect receiver (`cspot-ios3`) + cross-toolchain patches |
| `airplay/`    | AirPlay v1 / RAOP receiver (`fidelio-raop`) |
| `nowplaying/` | UIKit now-playing app (cover art, playback info, technical tab) |
| `audiod/`     | **Arbiter** — the one process that owns the AudioQueue; sources stream PCM to it over a Unix socket, most-recent-source-wins (see `audiod/DESIGN.md`) |

Each component keeps its own README; the directories preserve the git history of the
three original repos (fidelio-ios3-spotify / -airplay / -nowplaying).

## Toolchain

Linux x86_64 → ARMv6 Mach-O for iOS 3.1.3 (cctools-port ld64, clang 18, ldid fake-sign,
static libc++ 18.1.3, mbedTLS 3.6.7). Builds run on the cloud build session; see
`spotify/` for the toolchain patches and build scripts.
