# Docked-iPhone audio receiver

An audio-receiver stack for a jailbroken 2007 iPhone (iPhone1,1 · ARMv6/ARM11 · iOS 3.1.3),
permanently docked in and built for use with a Philips Fidelio DS9000 speaker dock, turning it
into a headless audio-receiver appliance.

> Independent hobby project, not affiliated with or endorsed by Philips. "Fidelio" is a trademark
> of its owner and is used here only to describe the speaker dock this software drives.

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
static libc++ 18.1.3, mbedTLS 3.6.7). `tools/build-arm.sh` builds and verifies every binary
and `tools/run-host-tests.sh` runs the host suites; `spotify/BUILD.md` documents the toolchain,
and `docs/HANDOFF.md` has the project state and conventions.

## Building

```sh
export PATH=/path/to/armv6-ios3-toolchain/bin:$PATH   # toolchain revision 3 (toolchain/README.md)
tools/build-arm.sh                                    # binaries and report in ../build/arm-out
```

Apple's iPhone SDK is not needed and not included. Without `IOS3_SDK`, `tools/build-arm.sh` makes
the project's own sysroot (`toolchain/sysroot/`): stub libraries from a symbol list, headers from
Apple's published open-source releases (fetched once, pinned by commit and sha256) and framework
headers of this project. It needs Ubuntu 24.04's `clang-18 llvm-18 cmake ninja-build
protobuf-compiler python3-protobuf libavahi-client-dev g++ unifdef flex bison`. A developer who has
Apple's `iPhoneOS3.1.2.sdk` can build against it with `IOS3_SDK=/path/to/iPhoneOS3.1.2.sdk`; that is
needed only to confirm additions to the sysroot's symbol list and headers
(`toolchain/sysroot/README.md`).
