# fidelio-ios3-spotify

A **Spotify Connect receiver for the original 2007 iPhone** (iPhone1,1, ARMv6 / ARM11, iOS 3.1.3), built with a Linux-hosted cross-toolchain. It turns a jailbroken first-generation iPhone — here, one docked permanently in a Philips Fidelio DS9000 speaker — into a headless Spotify Connect endpoint that logs in, fetches tracks, decodes Vorbis, and plays through the system AudioQueue.

Verified end-to-end on real hardware: Zeroconf login → AP authentication → Web-API token → CDN fetch → Vorbis decode → **320 kbps audio out the speaker**, at ~2.8 MB resident and ~20 % CPU on the 412 MHz ARM11.

> This repository is the **port**: the cross-toolchain patches, the glue/CLI sources, and build/run docs. It does **not** vendor the large upstream trees (cspot, bell, mbedTLS, cctools-port, LLVM) — you fetch those and apply the patches here. See [`BUILD.md`](BUILD.md) for the toolchain and [`CSPOT.md`](CSPOT.md) for the receiver.

## What's here
- `patches/` — the minimal patches that make the toolchain and the player work on iPhone OS 3:
  - `cctools-port-0001-no_version_load_command.patch` — ld64 option to omit `LC_VERSION_MIN_*` (2009-style Mach-O).
  - `ldid-0001-LDID_CD_VERSION.patch` — `LDID_CD_VERSION` env to emit a pre-2011 CodeDirectory (SHA-1) signature.
  - `llvm-project-18.1.3-0001-libcxx-libcxxabi-ios3.patch` — static libc++/libc++abi for armv6 iOS 3 (aligned_alloc, clock_gettime→mach_absolute_time, SjLj `_Unwind_DeleteException`, `remove_all` without `openat`).
  - `cspot-squeezelite-ee1720c-0001-armv6-ios3.patch` — the player: no-libdispatch semaphore, ARM endian header, codec-glob fix, flushed logging, small thread stacks, civetweb SO_REUSEPORT + thread cap, the **540 MB → 132 KB `CentralAudioBuffer` fix**, and the status writer.
- `cli-src/` — the command-line receiver written for this port: `main.cpp`, `CliPlayer.{h,cpp}`, `AudioQueueSink.{h,cpp}` (AudioToolbox sink), `StatusWriter.{h,cpp}` (writes `status.json`/`status.plist`/`cover.jpg`), `ios3_trace.h`, `CMakeLists.txt`.
- `tls_test.c` — an mbedTLS smoke test (TCP + TLS 1.3 handshake + HTTP GET) used to validate the crypto path on-device.
- `BUILD.md` — how the ARMv6/iOS-3 cross-toolchain is built (cctools-port ld64 + apple-libdispatch + ldid + stock clang 18 + static libc++).
- `CSPOT.md` — the receiver: which cspot fork, dependencies, build commands, memory/CPU notes, and what was verified on hardware.

## Build (summary)
```sh
export IOS3_SDK=/path/to/iPhoneOS3.1.2.sdk PATH=<toolchain>/bin:$PATH IOS3_CXX_RUNTIME=libc++ IOS3_COMPAT_INCLUDE=1
# mbedTLS (v3.6.7) and the cspot tree (squeezelite-esp32 components/spotify/cspot @ ee1720c + the patch here)
# then build the CLI from cli-src/ (expects the cspot tree at ../cspot). Full commands in CSPOT.md / BUILD.md.
armv6-ios3-strip -x cspot-ios3 && armv6-ios3-ldid -S cspot-ios3 && armv6-ios3-check cspot-ios3
```

## Run
```sh
./cspot-ios3 --client-id <spotify-app-id> --client-secret <spotify-app-secret> -b 320 -n "iPhone Fidelio"
```
Needs a Spotify **Premium** account and a Spotify developer app's client-id/secret (client-credentials flow, for the Web-API token). Then pick the device in any Spotify client on the same Wi-Fi. Status is written to `--status-dir` (default `/var/mobile/fidelio`) as `status.json` + `status.plist` + `cover.jpg` for the companion display app.

## Hardware prerequisites
Original iPhone (iPhone1,1) jailbroken on iOS 3.1.3 (e.g. redsn0w 0.9.3 pointed at the 3.1.2 IPSW), with OpenSSH. The binary is fake-signed with `ldid -S`; it runs as the `mobile` user.

## Credits & licenses
This port stands on:
- [cspot](https://github.com/feelfreelinux/cspot) / the [squeezelite-esp32](https://github.com/sle118/squeezelite-esp32) fork of it, and its `bell` library
- [Mbed-TLS](https://github.com/Mbed-TLS/mbedtls) (Apache-2.0)
- [cctools-port](https://github.com/tpoechtrager/cctools-port) and [apple-libdispatch](https://github.com/tpoechtrager/apple-libdispatch)
- [ldid](https://github.com/ProcursusTeam/ldid)
- [LLVM](https://github.com/llvm/llvm-project) libc++/libc++abi (Apache-2.0 WITH LLVM-exception)

Each `patches/*.patch` is a derivative of the named upstream and is governed by that upstream's license. The original files in this repo (`cli-src/`, `tls_test.c`, docs) are released under the MIT License — see [`LICENSE`](LICENSE). **No Spotify credentials are stored in this repo**; they are passed at runtime.
