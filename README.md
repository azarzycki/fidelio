# dock-receiver

A headless audio receiver for jailbroken iOS devices that sit in a speaker dock: Spotify Connect and
AirPlay v1 in, the dock's speakers out, with a now-playing display on the device's screen and on its
lock screen, and the dock's IR remote driving playback. It is built for, and used with, a Philips
Fidelio DS9000 speaker dock.

> Independent hobby project, not affiliated with or endorsed by Philips. "Fidelio" is a trademark
> of its owner and is used here only to describe the speaker dock this software drives.

## Hardware

| Device | OS | Notes |
|--------|----|-------|
| iPhone 2G (iPhone1,1, ARMv6/ARM11) | iPhone OS 3.1.3 | the first target; MobileSubstrate for the dock-remote tweak |
| iPad 1 (iPad1,1, A4) | iOS 5.1.1 | runs the same ARMv6 binaries; the dock remote is handled inside NowPlaying |

Every binary is built once, ARMv6 for iPhone OS 3.0 and later, and runs on both.

## Parts

| Dir | What |
|-----|------|
| `audiod/`      | `dock-audiod`, the **arbiter**: the one process that owns the audio output; the sources stream PCM to it over a Unix socket, most recent source wins (`audiod/DESIGN.md`, `audiod/README.md`). Also `dock-lockctl` (turns the lock-screen plugin on and off) and `probe/` (`voltest`, a volume probe) |
| `spotify/`     | `cspot-ios3`, the Spotify Connect receiver (cspot, patched), and the cross-toolchain patches (`spotify/CSPOT.md`, `spotify/BUILD.md`) |
| `airplay/`     | `dock-raop`, the AirPlay v1 / RAOP receiver (`airplay/AIRPLAY.md`) |
| `nowplaying/`  | `NowPlaying.app`, the dock display (cover art, track, a technical tab, settings), and `NowPlaying.lockbundle` / `NowPlaying.bundle`, the lock-screen plugin (`nowplaying/APP.md`) |
| `tweak/DockRemote/` | `DockRemote.dylib`, a MobileSubstrate tweak in iapd that turns the dock remote's keys into audiod controls (iPhone OS 3) |
| `common/`      | code the programs share: the audiod wire framing, the socket sink, the settings registry, the data directory (`common/DataDir.h`) and the SpringBoard entitlements |
| `toolchain/`   | the armv6-ios3 toolchain's own sources and the project's sysroot (`toolchain/README.md`) |
| `tools/`       | `build-arm.sh` (the ARM build and its checks), `build-debs.sh` (the two packages) and `run-host-tests.sh` (the host test suites) |
| `packaging/`   | the packages' install scripts (the migration), launchd jobs, `install.sh`, the third-party notices of the device binaries, and their tests (`packaging/README.md`) |
| `docs/`        | `docs/HANDOFF.md`: the project's state, conventions and device notes |

Everything on a device keeps its files in one directory, `/var/mobile/dockreceiver` (the socket,
status files, covers, `settings.conf`); `common/DataDir.h` is the one place that names it. The names
before the B1.17 rename are listed in [`RENAME.md`](RENAME.md).

## Toolchain

Linux x86_64 → ARMv6 Mach-O for iOS 3.1.3 (cctools-port ld64, clang 18, ldid fake-sign,
static libc++ 18.1.3, mbedTLS 3.6.7). `tools/build-arm.sh` builds and verifies every binary
and `tools/run-host-tests.sh` runs the host suites; `spotify/BUILD.md` documents the toolchain,
and `docs/HANDOFF.md` has the project state and conventions.

## Building

```sh
export PATH=/path/to/armv6-ios3-toolchain/bin:$PATH   # toolchain revision 3 (toolchain/README.md)
tools/build-arm.sh                                    # binaries and report in ../build/arm-out
tools/build-debs.sh                                   # the two .deb packages in ../build/debs
tools/run-host-tests.sh                               # the host suites (about 12 minutes)
```

The toolchain tarball is published as a release of this repository, tag `toolchain-r3`:
<https://github.com/azarzycki/fidelio/releases/tag/toolchain-r3>. The release notes give its size and
sha256. Unpack it anywhere and put its `bin/` in `PATH`; it also needs the host's `clang-18`.

Apple's iPhone SDK is not needed and not included. Without `IOS3_SDK`, `tools/build-arm.sh` makes
the project's own sysroot (`toolchain/sysroot/`): stub libraries from a symbol list, headers from
Apple's published open-source releases (fetched once, pinned by commit and sha256) and framework
headers of this project. It needs Ubuntu 24.04's `clang-18 llvm-18 cmake ninja-build
protobuf-compiler python3-protobuf libavahi-client-dev g++ unifdef flex bison`. A developer who has
Apple's `iPhoneOS3.1.2.sdk` can build against it with `IOS3_SDK=/path/to/iPhoneOS3.1.2.sdk`; that is
needed only to confirm additions to the sysroot's symbol list and headers
(`toolchain/sysroot/README.md`).

## Device setup

The devices must be jailbroken, with OpenSSH. The suite is installed as two Debian packages,
`dock-receiver` and, on iPhone OS 3, `dock-receiver-dockremote`: `tools/build-debs.sh` builds them from
the ARM build, `packaging/install.sh root@<device>` installs them from a PC (dry run first), and their
install scripts also migrate a device from the hand-installed setup of before B1.17 and keep it
restorable. See [`packaging/README.md`](packaging/README.md): contents, install, dry run, the Spotify
credentials file, rollback, cleanup, and what needs a SpringBoard restart or an undocked iapd restart.
What goes where on a device, and how to replace a running binary safely, is also in `docs/HANDOFF.md`
(§8 device work, §9 files on the devices). The Spotify receiver needs a Spotify Premium account and a
Spotify developer app's client id and secret, kept in a file on the device; no credential is stored in
this repository or in a package.

## Licence

The project's own code is MIT; `cspot-ios3` as a whole is distributed under GPL-3.0-or-later because
it links the Spotify library. See [`LICENSE`](LICENSE).
