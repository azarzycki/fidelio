# HANDOFF — the dock-receiver stack (repository `azarzycki/fidelio`), state as of 2026-11-22, night (B1.17 built, not yet on a device)

This file is written for a fresh session that has none of the conversation behind it. It lives in the
repo and is the one record of the project's state: **update it in the same commit as each round**
(§5.1.6). History before 2026-11-18 is in
[`docs/history/STATUS-2026-11-18.md`](history/STATUS-2026-11-18.md).

- **The owner** is the person whose project, devices and PC these are.
- **One session since 2026-11-18.** Until then two AI sessions worked on the project: a cloud build
  session, and a device session on the owner's Windows PC (cspot-build-local in the history) that relayed
  device results. Now one session, on the owner's PC, builds locally, commits, pushes `main` and installs
  on the devices.
- The state record used to live in a private store outside the repo. That store is retired; what is worth
  keeping is in this directory.
- The facts below were checked against the repo or a device report when they were first written down.
  Device-reported facts say so.
- **Names.** Since B1.17 (2026-11-22) the project's own names no longer use "Fidelio", which is Philips'
  product name: the project is `dock-receiver`, the programs are `dock-audiod`, `dock-raop`,
  `dock-lockctl` (and `cspot-ios3`), the ids are `com.dockreceiver.*`, the data directory is
  `/var/mobile/dockreceiver`. The full old → new list is [`RENAME.md`](../RENAME.md). **The devices
  still run the old names** (`fidelio-audiod`, `/var/mobile/fidelio`, `com.fidelio.*`, `Fidelio.bundle`)
  until they are migrated by the packages (§2.4, §3.0). This file uses the new names for the tree and
  the build, and the old ones where it describes what a device runs today or quotes what a device
  logged.

**Read in this order:**
1. §0, the resume checklist.
2. §2, the current state.
3. §3 and §4, the open work and the questions waiting for the owner.
4. §5, the rules and decisions.
5. §6 and §7, the build conventions and the round checklist.

§8–§12 are reference. Other documents:
- [`docs/RENAME-PLAN.md`](RENAME-PLAN.md): the plan for B1.17, the rename, and what was delivered
  against it (done in the tree; the devices not yet migrated).
- [`RENAME.md`](../RENAME.md): every old name and its new one.
- [`packaging/README.md`](../packaging/README.md): the two packages, the install that is the migration,
  rollback and cleanup, the Spotify credentials file.
- [`packaging/THIRD-PARTY-NOTICES.md`](../packaging/THIRD-PARTY-NOTICES.md): the third-party code linked
  into each device binary, from the link maps (facts, no judgement).
- [`docs/history/STATUS-2026-11-18.md`](history/STATUS-2026-11-18.md): the frozen per-round record with every
  earlier hash. Tasks 36 … 23 come first, newest on top; the older tasks follow.
- Component docs: [`spotify/BUILD.md`](../spotify/BUILD.md) (the toolchain),
  [`spotify/CSPOT.md`](../spotify/CSPOT.md), [`airplay/AIRPLAY.md`](../airplay/AIRPLAY.md),
  [`nowplaying/APP.md`](../nowplaying/APP.md), [`audiod/README.md`](../audiod/README.md),
  [`audiod/DESIGN.md`](../audiod/DESIGN.md), [`tweak/DockRemote/README.md`](../tweak/DockRemote/README.md).

---

## 0. Resume checklist (one session, on the owner's Windows PC)

1. **The repo:** the Windows clone is `C:\code\fidelio\github\fidelio` (the folder keeps its old name on
   purpose, §6.1); its `origin` is GitHub (`azarzycki/fidelio`; the repository's rename is a question for
   the owner, §4). Run `git fetch` first and compare with `origin/main` (§2.1).
2. **The build environment** is a WSL2 distro on the same PC (§6.1): toolchain, a second clone and
   `BUILD_ROOT` at the reference paths. Since B1.17 the reference clone is `/home/user/dock-receiver`
   (the old `/home/user/fidelio` is kept but no longer the reference). Apple's SDK is no longer needed
   to build (§6.3).
   - Bring the WSL clone up to date: fetch and fast-forward on the Windows side first, then `git pull` in
     WSL.
   - Builds and test runs go in subagents, one at a time (§6.1).
   - Since 2026-11-22 all work goes to background subagents, device work and log analysis included; the
     session plans, briefs, reviews and talks with the owner, and ends its turns at once (§5.2, §6.1).
3. **Prove the baseline:**
   - `tools/build-arm.sh` → `result: OK` with the hashes in §2.3 (first). The host suite needs its output
     (`pkg_debs` packages `$BUILD_ROOT/arm-out`).
   - `tools/build-debs.sh` → the two packages and their `SHA256SUMS` in `$BUILD_ROOT/debs` (§2.3, §6.7).
   - `tools/run-host-tests.sh` → `TOTAL host: 928 ok, 0 FAIL`, 30 suites, about 13 minutes. Run it alone,
     with nothing else heavy on the PC (§6.4).
4. Then work through §3 in order. First: B1.17 is built and **not yet on a device**; the devices still
   run the old names (§2.4, §3.0).

---

## 1. The project in one page

A headless audio receiver for two jailbroken devices docked in a **Philips Fidelio DS9000** speaker dock.
"Fidelio" is Philips' product name and is used only descriptively (§5.3).

| Device | Model | OS | CPU | Notes |
|---|---|---|---|---|
| the **phone** | iPhone 2G (iPhone1,1) | iPhone OS 3.1.3 | ARMv6 (ARM1176) | MobileSubstrate present (stub + libsubstrate; not registered with dpkg); **no cycript**; has syslog; no `ps` |
| the **iPad** | iPad1,1 | iOS 5.1.1 | ARMv7 (A4); runs our armv6 binaries | **no MobileSubstrate** (deliberately; the install was stopped); **has cycript**; **no syslog** (use file traces); ~13 MB free RAM; lacks head/tail/tr/cut/sort/uniq/awk/nohup/expr/stat/id/wc/basename/env/tee/du |

Both have dpkg 1.18.10 (`iphoneos-arm`), `/bin/sh` = bash 4.0.17, gzip/bzip2/lzma but no xz, and no
awk, plutil or defaults (the survey of 2026-11-22, §2.5).

**Components** (one monorepo, everything built ARMv6 for iPhone OS 3.1; since 2026-11-22 against the
project's own sysroot, before that against Apple's iPhoneOS 3.1.2 SDK, with identical binaries; §6.3).
Names as in the tree since B1.17; the old name of each is in brackets (what the devices run today, §2.4):
- **`dock-audiod`** (`audiod/`; was `fidelio-audiod`): the arbiter daemon.
  - It owns the AudioQueue output. Sources stream PCM to it over the Unix socket
    `/var/mobile/dockreceiver/audiod.sock` (was `/var/mobile/fidelio/audiod.sock`). The data directory is
    one compile-time default, `DOCKRECEIVER_DIR` in `common/DataDir.h`.
  - It picks the active source, applies volume, wakes and unlocks the display on play, enables the lock
    plugin, and routes transport controls.
  - It is signed with the 4 SpringBoard entitlements.
- **`cspot-ios3`** (`spotify/cli-src`, on a patched upstream cspot): the Spotify Connect receiver.
- **`dock-raop`** (`airplay/`; was `fidelio-raop`): the AirPlay v1 (RAOP) receiver, with DACP for
  transport.
- **`NowPlaying.app`** (`nowplaying/src`): the dock display, version 1.5.0 since B1.17.
  - Tabs: Now Playing, Technical, and Settings (built from the daemons' manifests).
  - One universal binary (B1.18).
  - Bundle id `com.dockreceiver.nowplaying` (was `com.fidelio.nowplaying`); URL scheme `nowplaying:`
    (unchanged).
  - 1.5.0 shows "Daemons not running" when the data directory is missing or nothing in it is fresh.
- **`dock-lockctl`** (was `fidelio-lockctl`): a CLI to enable/disable the lock plugin; entitled like
  audiod.
- **Lock-screen plugin** (`nowplaying/lockplugin`): `NowPlaying.lockbundle` on the iPad,
  `NowPlaying.bundle` on the phone (were `Fidelio.lockbundle` and `Fidelio.bundle`); same binary, bundle
  id `com.dockreceiver.lockplugin`. It shows the track on the lock screen and keeps it lit.
- **`DockRemote.dylib`** (`tweak/DockRemote`, phone only): a MobileSubstrate tweak in iapd that turns the
  dock's IR remote play/pause/next/prev into audiod controls.
- **`voltest`** (`audiod/probe`): a device probe of the system-volume categories (diagnosis tool).
- **Today** the daemons run on the devices under the **owner's own hand-made launchd jobs**
  (`com.fidelio.audiod`, `.cspot`, `.raop`, `.keepalive`), installed by hand (§2.4, §8).
- **Since B1.17 the suite is packaged** (`packaging/`, `tools/build-debs.sh`): two Debian packages,
  version 1.5.0-1, `dock-receiver` (every supported OS: the daemons, the jobs
  `com.dockreceiver.audiod`, `.cspot`, `.raop`, `.keepalive`, the app, the lock plugin, the
  `dock-receiver-setup` command) and `dock-receiver-dockremote` (the tweak; iPhone OS 3.x only). Their
  install scripts are the migration from the hand-installed setup (§2.4, §3.0, §8). Not yet installed on
  a device.

The **dock**:
- It plays the phone over a USB digital route, which reports **no volume control** (§3.1).
- Its own buttons drive its amplifier; the phone only mirrors that level.
- Its IR remote's keys reach iapd over iAP.

---

## 2. Current state

### 2.1 Git
- `main`: see `git log`. Since 2026-11-18 it gained `6721da9` (tools only), `dfbbc3d` (this record moved
  into the repo), `b8d8976` (the volume probe extended), `5a2283c` (toolchain revision 2), `c46beee`
  (toolchain revision 3, §6.1), `d1b20a9` (the licence note for the Spotify receiver, §5.2) and the docs
  commits around them, `e590ef1` (B1.16.12), `619c552` and `8af19cb` (B1.16.13), `5f519ec` and `23f407d`
  (B1.16.14), `86ce0b7` (B1.16.15), `59d03a1` (B1.16.16) and its docs commit `51b22fe`.
  **Before B1.17, `main` was at `51b22fe`** (2026-11-22).
- **B1.17** (2026-11-22, the rename and the packages) was made on the branch `b117-rename`: `8ab46f4`
  (part A, the rename in the tree), `7d1ced6`, `307be01`, `1f7d18d` (part B, the third-party notices,
  the packages, their host suites) and the docs commit that follows them (this record under the new
  names). **B1.17 = `8ab46f4`..`1f7d18d` plus that docs commit.** Right after the docs commit the
  session fast-forwards `main` to the branch and pushes it with the marker branch `audiod-b117`
  (§3.0); `git log` and `git ls-remote --heads origin` show whether that has happened. Nothing of
  B1.17 has run on a device.
  - No source of a binary that is installed on a device changed since `5dc865f`, until B1.16.12 (audiod,
    e590ef1, below).
  - Since `c46beee` every binary builds to a new sha256, because the toolchain puts a notice into it
    (§2.3). Those builds are on both devices since 2026-11-22: the iPad has those of the morning, the
    phone those of B1.16.12 … B1.16.15 as well (§2.4).
  - `55d1a82`, `926ea54`, `831ac15` and `b5e3106` (2026-11-22): the build no longer needs Apple's SDK
    (§6.3). No binary changed.
  - `b2a8ee3` (2026-11-22): the docs name where the toolchain is published. The tag **`toolchain-r3`**
    points at it, and the release of that name holds the toolchain tarball (§6.1).
- **B1.16.12** (2026-11-22, §3.1) is on `main` (e590ef1, marker `audiod-b11612`). Its device test on the
  phone passed the same day (§2.5). Only `fidelio-audiod` changed (§2.3).
- **B1.16.13** (2026-11-22, §2.5, §3.2) is on `main` (619c552, 8af19cb, marker `audiod-b11613` at
  8af19cb). Installed on the phone that day. Only `DockRemote.dylib` and `NowPlaying.app/NowPlaying`
  changed (§2.3).
- **B1.16.14** (2026-11-22, §3.1) is on `main` (5f519ec, 23f407d, marker `audiod-b11614` at 23f407d).
  Installed on the phone that evening; its AirPlay device test is still owed. Only `fidelio-audiod`
  changed (§2.3).
- **B1.16.15** (2026-11-22, §2.5, §3.2) is on `main` (86ce0b7, marker `audiod-b11615`). Installed on the
  phone that evening; the first swallow test passed for previous and next (§2.5). Only
  `DockRemote.dylib` changed (§2.3).
- **B1.16.16** (2026-11-22, §2.5, §3.2) is on `main` (59d03a1, marker `audiod-b11616`). Installed on the
  phone that evening (19:28–19:32 phone time); the owner's paused-target test passed the same evening
  (§2.5, §3.2). `fidelio-audiod`, `DockRemote.dylib` and `NowPlaying.app/NowPlaying` changed (§2.3).
- In the B1.16.x items above and in §2.2 the binaries keep the names they had then (`fidelio-audiod`
  is today's `dock-audiod`, and so on; `RENAME.md`).
- The owner said "You do the git updates": each round, fast-forward `main` and add a marker branch (§7).
- **Marker branches**, one per round:
  - Earliest rounds: `audiod-b11`, `-b12`, `-b13`, `-b15` … `-b19`, `-b110` … `-b116`. There is no `-b14`.
  - `audiod-b1161` (32af243), `-b116dr` (272c7e5), `-b116lp` (0b90521).
  - The second cloud session's: `-b1164` (ab0c7aa), `-b118`, `-b1165`, `-b1166`, `-b1167`, `-b1168`,
    `-b1169`, `-b11610`, `-b11611`.
  - This session's: `-b11612` (e590ef1), `-b11613` (8af19cb), `-b11614` (23f407d), `-b11615` (86ce0b7),
    `-b11616` (59d03a1), and `-b117` (B1.17's docs commit, set when `main` is fast-forwarded to it).
  - The working branch `b117-rename` holds B1.17 until `main` is fast-forwarded to it.
  - voltest (5dc865f, b8d8976) and the tools and toolchain commits (6721da9, 5a2283c, c46beee, and the
    four sysroot commits 55d1a82, 926ea54, 831ac15, b5e3106) have no marker. Check with `git ls-remote --heads origin`.
- The old cloud session branch `ccr-afef93fb-x0jdve` is history, at `5dc865f`.
- **Patches:** the series `diffs/fidelio-0001…0049` (its old name), one per commit, existed only in the
  retired private store. The commits are the record.
- **Old ids:** the first session's local commit ids appear in old binary names and do not exist on
  GitHub. Map (old → GitHub): bf61abb→a5ba534, 49ec495→32af243, e1ae8c3→272c7e5, fedec9b→0b90521,
  3aaba31→9698db9. The full table is in the history file, Task 23.

### 2.2 Rounds delivered since 2026-11-15
The patch numbers are those of the retired series (§2.1); the history file refers to them.

| Round | Commit | Patch | What | Device status (as reported) |
|---|---|---|---|---|
| B1.16.4 + tools | 880ae13, ab0c7aa | 0039, 0040 | unlock on play (SBSOpenSensitiveURLAndUnlock), the `nowplaying:` scheme; `tools/run-host-tests.sh`, `tools/build-arm.sh`, `tools/isa_scan.py` | superseded by B1.16.6 |
| B1.18 | 47d4760 | 0041 | one universal NowPlaying 1.4.0 (native iPad, rotation); cspot fetches 640 px covers (`cover_size`) | iPad native confirmed |
| B1.16.5 | b051bfc | 0042 | DockRemote finds `MSHookMessageEx` in libsubstrate | bound on the phone, but on the wrong method |
| B1.16.6 | aeed5b1 | 0043 | 4 entitlement keys; the accepted unlock request is the result (no launch probe); keep-lit (`restartDimTimer`) | **iPad unlock PASS**; installed on both |
| B1.16.7 | e3e448e | 0044 | lock plugin: phone layout (`LPLayout.c`) and 3.1.3 keep-lit fallback (`restartDimTimer:` float) | **phone PASS, iPad PASS**; installed on both |
| B1.16.8 | 4ed6a5a | 0045 | DockRemote hooks GraphicsServices `GSSendEvent` (observe-first) | the revision 3 build was loaded into iapd on the phone 2026-11-22; its observe run (two passes) gave the decode of §2.5; superseded on the phone that day by B1.16.13, then B1.16.15 |
| B1.16.9 | 8eec470 | 0046 | NowPlaying 1.4.1: the iPad dock remote as a staged media-controller prototype | installed on both devices 2026-11-22 (revision 3 build); on the phone replaced by 1.4.2 (B1.16.13) that day; not exercised on the iPad |
| B1.16.10 | 4158342 | 0047 | `wake_mode` launch\|screen (the lock plugin as backstop) | its audiod is superseded by B1.16.11's |
| B1.16.11 | 7330752 | 0048 | lock plugin follows a live setting change and a SpringBoard restart | installed on both devices 2026-11-22 (revision 3 build); not exercised on the iPad; on the phone its audiod was replaced that day by B1.16.12's, then B1.16.14's, which contain it |
| voltest | 5dc865f | 0049 | the volume probe (no marker) | ran on the phone once (paused) |
| tools | 6721da9 | none | cspot build date pinned (§6.3), host compiler check (§6.4); no binary changed | nothing to install |
| voltest | b8d8976 | none | the probe also shows iapd's side of the volume and can call `IAPAudioSetVolume` (§3.1) | on the phone since 2026-11-22; first readings in §2.5 and §3.1 |
| toolchain r2 | 5a2283c | none | toolchain revision 2: start-up objects built from Apple's open-source Csu, licence texts, `toolchain/pack.sh`, the toolchain's own sources in the repo under MIT, link-map and start-up checks in `build-arm.sh`; every binary keeps its sha256 | nothing to install |
| toolchain r3 | c46beee | none | toolchain revision 3: the start-up objects carry a notice that says where their source is (Apple's open-source licence asks for it "in the code itself"), so every binary carries it in a section `__TEXT,__csu_notice`; `build-arm.sh` requires revision 3 and checks the notice; every binary has the same size and a new sha256 (§2.3) | 2026-11-22: smoke test PASS and the suite installed on both devices (§2.4, §3.2) |
| sysroot | 55d1a82, 926ea54, 831ac15, b5e3106 | none | the build without Apple's SDK: `toolchain/sysroot/` makes a sysroot from a symbol list (stub libraries), Apple's published open-source headers (pinned) and framework headers of our own; `tools/build-arm.sh` uses it by default; every binary keeps its sha256 (§6.3) | nothing to install |
| B1.16.12 | e590ef1 | none | audiod tells the dock a source's volume: after a set of the system volume in absolute mode it calls `IAPAudioSetVolume` (IAP.framework, dlopen'ed) with the same level, while an accessory takes the volume; never for an external change; a drag is coalesced (one call per 200 ms, the latest level last); the `hwvolume` line ends `; dock=<result>`; host suite `dockvol` (§3.1, §10.4) | **phone PASS** 2026-11-22: the dock's loudness follows the Spotify slider (§2.5); AirPlay reaches the dock, with the echo problem B1.16.14 fixes (§2.5); on the phone replaced that evening by B1.16.14's audiod, which contains it; the iPad not installed, not tested |
| B1.16.13 | 619c552, 8af19cb | none | DockRemote: the decode measured on the phone (the code in the word @56, down and up in the flags, next and previous as one event), a press taken or passed whole with one verb, and the key log `remote-keys.log`; NowPlaying 1.4.2 shows the last six presses in the Technical tab; host suites `remote_keys` and `keylog` (§2.5, §10.5) | installed on the phone 2026-11-22 (DockRemote loaded 18:08:49 phone time; NowPlaying 1.4.2 stays); its key passes are in §2.5; its DockRemote superseded by B1.16.15 at 18:35:49, before the swallow test |
| B1.16.14 | 5f519ec, 23f407d | none | audiod ignores a source's echo of a level it reported to that source (within 3 s and one dock step, 1/255): no set, no dock call; a held dock level is dropped at its flush when the hardware level changed outside meanwhile, even before the poll has seen it; host suite `dockvol` 14 → 25 checks (§2.5, §3.1, §10.4) | installed on the phone 2026-11-22 evening; Spotify played through it without a problem; the AirPlay echo test is still owed (§3.1) |
| B1.16.16 | 59d03a1 | none | the remote keeps a paused source: audiod's **remote target** (the active source, or the paused last one until it disconnects, sends STOP, another source of ours plays, or audio not ours plays), written to `remote-target`; with nothing active every CONTROL verb goes to it (TOGGLE/PLAY as PLAY); DockRemote takes play/pause, next and previous while a target exists (falls back to `active-source` without the file); NowPlaying 1.4.3 keeps the paused source's track on screen; host suites `remote_core`, `remote_tgt`, `np_source` (§2.5, §5.2, §10.1, §10.5, §10.6) | installed on the phone 2026-11-22 19:28–19:32, DockRemote loaded 19:34:20; **phone PASS** 19:35–19:37: play/pause paused and resumed Spotify (both taken), and the Music app started by hand took the remote back (§2.5, §3.2); next/previous while paused, AirPlay, the iPad not yet tested |
| B1.16.15 | 86ce0b7 | none | DockRemote: codes 4 (the left key, measured) and 3 (the right key, assumed by symmetry when built; measured since, §2.5) are previous and next as well: taken like 21 and 20 under swallow, logged as `previous` and `next` (the names `rewind` and `seek-forward` are gone); host suite `remote_keys` 52 → 65 checks (§2.5, §10.5) | installed on the phone 2026-11-22, loaded 18:35:49; first swallow test 18:42: previous and next taken and sent to Spotify (§2.5); what was heard not yet reported; play/pause, AirPlay, no source active not yet tested (§3.2) |
| B1.17 | 8ab46f4, 7d1ced6, 307be01, 1f7d18d, and the docs commit after them | none | the rename `fidelio` → `dock-receiver` in the tree (`RENAME.md`): `dock-audiod`, `dock-raop`, `dock-lockctl`, `NowPlaying.bundle`/`.lockbundle`, `com.dockreceiver.*`, the data directory `/var/mobile/dockreceiver` as one compile-time default (`common/DataDir.h`), NowPlaying 1.5.0 with a "Daemons not running" state; the third-party notices (`packaging/THIRD-PARTY-NOTICES.md`); two reproducible Debian packages 1.5.0-1 whose install scripts are the migration (`packaging/`, `tools/build-debs.sh`); host suites `np_source` 15 → 23, new `pkg_setup` (243) and `pkg_debs` (77), 849 ok in 30 suites (§2.3, §6.4, §6.7) | **not yet run on a device**; staged on the PC in `C:/code/fidelio/cloud-build/deploy-b117/` (the two `.deb` files and `SHA256SUMS`); the migration is the first open item (§3.0) |

### 2.3 Builds

#### 2.3.1 Current: B1.17 at `c8b1475` (`tools/build-arm.sh`, toolchain revision 3, the project's own sysroot)

Not yet run on a device. From `/home/user/build/arm-out/SHA256SUMS` and `report.txt` of the build at
`1f7d18d` in the reference clone `/home/user/dock-receiver` (`result: OK`, `git status` clean):

| Binary | Size | sha256 |
|---|---|---|
| dock-audiod | 483,312 | `05e52c1347d6faa55036125b100162455d51ccea7eb4330e3d0fd7b239575276` |
| dock-lockctl | 163,024 | `b0d00be2f0cf82feae31ffdab4515e434b1038a463bf1fcfc65af4e3e9fdec29` |
| dock-raop | 942,208 | `5e38bd1e760984de33c8fbaee1c04522f85be39fff523ec5e9cd06d24bca9d43` |
| cspot-ios3 | 2,290,256 | `3c3370567df2a9075ae7c3712dbe2df888be77826a5fddd7ad3758f5efbaa074` |
| NowPlaying.app/NowPlaying (1.5.0) | 127,472 | `ea218110523c6f8a9660e53f10004ac913e7e1b21147d624f6edf04b81c13033` |
| NowPlaying.lockbundle/NowPlaying = NowPlaying.bundle/NowPlaying | 56,368 | `46d1133759ff3dcbb3cfa6a6062a8995b5424e62074dacb446b9ff7a0af04288` |
| DockRemote.dylib | 35,344 | `1b7232bd04934aefbdbddafb547b44e924d5f8eacc624713a96e9926dc4de175` |
| voltest | 51,200 | `c4d1c4246bfed1c829a7b21f1c7df804b60b7c61f0b6110b8eeb3bbe116e111d` (unchanged since b8d8976) |

- Every binary except voltest has a new sha256: the names, ids and paths inside them changed. Sizes
  against B1.16.16: audiod +976 bytes, raop +96, cspot-ios3 +96, the lock plugin +64; lockctl,
  NowPlaying and DockRemote kept their sizes. The lock bundles report `FAIL(2 by-design)` as before; no
  new warning.
- The ARM build at `8ab46f4` ran twice from nothing with identical hashes (`result: OK`); part B
  (`7d1ced6` … `1f7d18d`) changed no binary.
- `cspot-ios3`'s hash depends on the repo path, which is now `/home/user/dock-receiver` (§6.1, §6.3);
  DockRemote.dylib's depends on `$BUILD_ROOT/arm/dockremote` as before.

**The packages** (`tools/build-debs.sh` at `c8b1475`, from that build; staged on the PC in
`C:/code/fidelio/cloud-build/deploy-b117/` with `SHA256SUMS`). These are the hashes **at `c8b1475`**
(after the review's fixes, §3.0):

| Package | Size | sha256 |
|---|---|---|
| `dock-receiver_1.5.0-1_iphoneos-arm.deb` | 1,844,346 | `413e70cec5dea4ae10197592e171698e3b235193d800c0d5f97988f7421c28a3` |
| `dock-receiver-dockremote_1.5.0-1_iphoneos-arm.deb` | 25,522 | `cbc455490f09e57029348dfe9b933b1364bd57b1c5e5eb6369944814932a8614` |

Built twice by the script with the same sha256 (reproducible, §6.7); the binaries inside are those of
the table above, byte for byte.

#### 2.3.2 The binaries on the devices today (B1.16.16 and revision 3, old names)

The devices still run these (§2.4). This is the record of B1.16.16 and earlier, kept with the old file
names, because a device report shows them.

The sources of each binary are those of the round named in the last column. Since c46beee only the
toolchain changed, B1.16.12 changed audiod, B1.16.13 changed DockRemote and NowPlaying, B1.16.14
changed audiod again, B1.16.15 changed DockRemote again, and B1.16.16 changed all three (on the phone
since the evening of 2026-11-22; the hashes marked "on the phone" below are what it had before).

| Binary | Size | sha256 (toolchain revision 3) | Round |
|---|---|---|---|
| fidelio-audiod | 482,336 | `9328a974e1eb1c31928aa08a118a42b0344888e373e1d7bacb3dfb7973a59164` | B1.16.16 (the remote target; on the phone); before it, B1.16.14 23f407d (the echo filter; on the phone until B1.16.16): 481,856, `a8de4d6c741e7efff581556e151bd912c3d426afca0a54608212a8ca18dd2ca1`; before it, B1.16.12 e590ef1 (the dock forward): 481,680, `7c04dc0b61e15bd308a7a5f5c9d2071fca66007dc394cea6aed97086ad2f1923`; before that, B1.16.11 7330752 (on the iPad): 464,960, `30eb78eb0f6edb919d8091a2793e7097e2b5bcaf68bb60a690c878b85b89afe1` |
| fidelio-lockctl | 163,024 | `704c799f8b15122d8921cbdd9609c0e8136a85d038cc1a9c019b2c929db1ed12` | B1.16.6, aeed5b1 |
| fidelio-raop | 942,112 | `baf304f686d4bc63b8737942e935ba812746eb5b5451efe7c725daef3f07d132` | B1.16, a5ba534 (old id bf61abb; raop unchanged since) |
| cspot-ios3 | 2,290,160 | `35de013e2141c6a26cf8fe6a429f2f75cf99f9a75576c38720a1e1918e4b74fb` | B1.18, 47d4760 |
| NowPlaying.app/NowPlaying | 127,472 | `bc8de26e7cecaee42ce62c3ac5bb5a8d483933c3bb84daeefe7ca0f42e9f2fd8` | B1.16.16 (1.4.3; on the phone); before it, B1.16.13 619c552 (1.4.2; on the phone until B1.16.16): 127,296, `09a4d05821cea611a87f00ddc0812721d41757a6733fe84571a7eba8cb732cad`; before it, B1.16.9 8eec470 (1.4.1; on the iPad): 109,856, `35bfa2294eca5fe00cb9d5e08a0056fc0b839f6831ce6b411191b4cd9e4ef256` |
| Fidelio.lockbundle/Fidelio = Fidelio.bundle/Fidelio | 56,304 | `e708fab07b256ab3ae39f19eff40d1ddfbf7f245bd42a04435ea2b2c728a2df6` | B1.16.7, e3e448e |
| DockRemote.dylib | 35,344 | `6a0acbd0a22dd703ee7251cab6e48fec2bd73908201a8c289acffce6e605b38e` | B1.16.16 (on the phone); before it, B1.16.15 86ce0b7 (on the phone until B1.16.16): 35,104, `66d34a6e5db58e8b7b0392082fa90ff436b68b8c9b023b563ad913d89e075537`; before it, B1.16.13 619c552: 35,104, `6206f01478499936b815ecce6924abcd3375ef3531a84c2c55b3aa8a4c81da5b`; before that, B1.16.8 4ed6a5a: 34,640, `8427742ca0e70a20c03c7d09884c47467169cb44d9c3efa0fedbb4981483621b` |
| voltest | 51,200 | `c4d1c4246bfed1c829a7b21f1c7df804b60b7c61f0b6110b8eeb3bbe116e111d` | probe, b8d8976 |

- All eight reproduce from nothing, on any day, at the reference paths of that time (the clone
  `/home/user/fidelio` at `59d03a1`, §6.1). Two builds from nothing on 2026-11-22 gave the same nine
  files. cspot-ios3's and DockRemote.dylib's hashes depend on those paths (§6.3).
- That build's list is kept in `/home/user/build.b11616-fidelio/arm-out/SHA256SUMS` (the last build
  under the old names).
- **B1.16.12's audiod is 16,720 bytes larger.** Its `__text` grew by about 3.7 KB and `__cstring` by
  about 0.8 KB, and `__TEXT` crossed a 16 KB segment boundary (368 KB to 384 KB). It imports no new
  symbol (the indirect symbol tables of the two builds list the same names).
- **B1.16.13:** DockRemote.dylib grew by 464 bytes (34,640 to 35,104) and NowPlaying by 17,440 (109,856 to
  127,296). Neither imports a symbol that was not in the stub manifest already; the manifest's
  "imported by" fields were brought up to date (`imports.py update`, no symbol added). Every other
  binary kept its sha256.
- **B1.16.14:** audiod grew by 176 bytes (481,680 to 481,856). No new import. Every other binary kept its
  sha256.
- **B1.16.15:** DockRemote.dylib kept its size (35,104) and got a new sha256 (two more cases in each of two
  switches, the load line). No new import. Every other binary kept its sha256.
- **B1.16.16:** audiod grew by 480 bytes (481,856 to 482,336), DockRemote.dylib by 240 (35,104 to 35,344),
  NowPlaying by 176 (127,296 to 127,472). No new import: the stub manifest's "imported by" fields were
  brought up to date (`imports.py update`: `_strlen` now also by NowPlaying, `_strncpy` also by
  DockRemote). A first build of DockRemote with a function-local static for its paths was 156,912 bytes:
  the guard variable pulled libc++abi's guard and exception code in. The paths are a constant-initialised
  table now, and the link map lists no libc++ member, as before. Every other binary kept its sha256.
- **Built without Apple's SDK since `b5e3106`** (§6.3): the same nine files, byte for byte, as the build
  against the SDK.
- **What differs from the revision 2 build of the same sources:** one more section in each binary,
  `__TEXT,__csu_notice` (388 bytes in an executable, 313 in a dylib or a bundle), which holds the notice
  of the start-up object (§6.3).
  - No file size changed: the notice fits into the padding of `__TEXT`.
  - `__text` and the stubs start at a lower address, so address-dependent bytes differ. Every other
    section has the size it had.
- **Which builds are on which device** (2026-11-22, evening; §2.4):
  - The phone has all eight files of the table above, each the one in its sha256 column: audiod,
    NowPlaying 1.4.3 and DockRemote of B1.16.16 (since 19:31 phone time), and the rest unchanged since
    the morning.
  - The iPad has the revision 3 builds installed that morning: lockctl, raop, cspot and the lock bundle as
    in the table, audiod of B1.16.11 (`30eb78eb`) and NowPlaying 1.4.1 (`35bfa229`), the earlier hashes
    in the last column. It has none of B1.16.12 … B1.16.15, and no DockRemote or voltest.

**The earlier hashes** are the ones the same sources gave with toolchain revisions 1 and 2, which built
identical bytes. They are what the cloud session published and what was installed on the devices until
2026-11-22 (the backups there, §2.4). A device report that shows one of them still identifies the round.

| Binary | sha256 with toolchain revisions 1 and 2 |
|---|---|
| fidelio-audiod | `dd2bc78706255db1114b1b21a1ec7f214d8177056c1eb4b370e5916ba1691e42` |
| fidelio-lockctl | `45da7833232a779f9dbf2969b19c7a1e8bac9d612b1c43a75f00c0ea5412a212` |
| fidelio-raop | `9edce5e531c02534244817d5050152851bd0d08cc4d49321a0b74800d103b89a` |
| cspot-ios3 | `2979aeddf6cebb5545bdbd9f5dd37b2eaf6d6f66cd8f623ab91cd64f76671f82` |
| NowPlaying.app/NowPlaying | `f23daec8999e7314fac17920e5c7b62856c7654c345266670836bf9b5504f273` |
| Fidelio.lockbundle/Fidelio = Fidelio.bundle/Fidelio | `559763a5b019d4f478eb86477bb37b02e3befbacffc86878b284d8fd7f927ae4` |
| DockRemote.dylib | `4ae947d5f0d2b55e04b6d780eeafab61741180eef0987052725b692bfa8ca639` |
| voltest | `8c9f1f0372f05643f34d088fc4d6b16538dbf5eefd2fb8354d16e851754299f1` (the 5dc865f one was 51,056, `bb956992…f956b9`) |

### 2.4 What runs on each device (the last device reports)

**Both devices still run the hand-installed setup under the OLD names: B1.17 changed nothing on them.**
What follows is true of the devices today: `fidelio-audiod`, `fidelio-raop`, `fidelio-lockctl` and
`cspot-ios3` in `/var/mobile/fidelio/b1/`, the data directory `/var/mobile/fidelio`, the owner's jobs
`com.fidelio.*` in `/Library/LaunchDaemons`, the lock plugin `Fidelio.bundle` (phone) or
`Fidelio.lockbundle` (iPad), the app id `com.fidelio.nowplaying`. Nothing of B1.17 has run on a device.
The layout after the migration is at the end of this section.

- **Phone, as of the evening of 2026-11-22 (19:40):** all eight files of §2.3's first table, each sha256
  read back on the device: audiod B1.16.16 `9328a974`, lockctl `704c799f`, raop `baf304f6`, cspot
  `35de013e` (47d4760), voltest `c4d1c424` (b8d8976), NowPlaying 1.4.3 `bc8de26e`, `Fidelio.bundle`
  `e708fab0`, DockRemote B1.16.16 `6a0acbd0`. **`remote-tweak.observe` and `remote-tweak.swallow` are
  set**: the takeover of play/pause, next and previous is on (§2.5, §3.2). At 19:40 Spotify was playing,
  `active-source` and `remote-target` both `spotify`. How it got there:
  - **Morning** (by this session, on the owner's "Update the iPhone and iPad"): the whole suite as built
    with toolchain revision 3: audiod `30eb78eb` (7330752), NowPlaying 1.4.1 `35bfa229` and the rest as
    above. The start-up smoke test passed first (§3.2). What was there before is kept beside each file:
    `<name>.pre-r3.bak` in `/var/mobile/fidelio/b1/`, `/Applications/NowPlaying.app.pre-r3`,
    `Fidelio.bundle.pre-r3`, `settings.conf.pre-r3`. Before: audiod `c23cd624` (aeed5b1), lockctl
    `45da7833`, raop `9edce5e5`, cspot `2979aedd`, NowPlaying 1.4.0, bundle `559763a5`.
  - **DockRemote 4ed6a5a** (`8427742c`, the revision 3 build) went into
    `/Library/MobileSubstrate/DynamicLibraries/` the same morning and was loaded into iapd by an undocked
    iapd restart; load line `DockRemote B1.16.8: hooked GSSendEvent with MSHookFunction from
    /usr/lib/libsubstrate.dylib`. The build before it is kept as `DockRemote.dylib.pre-r3.bak` (b051bfc).
    `remote-tweak.observe` was set then.
  - **audiod B1.16.12** (`7c04dc0b`, e590ef1) replaced 7330752's later that day, backup
    `fidelio-audiod.pre-b11612.bak`; then **audiod B1.16.14** (`a8de4d6c`, 23f407d) replaced it in the
    evening, backup `fidelio-audiod.pre-b11614.bak`. The daemons were restarted for each. The start-up
    line on the phone: `dock volume (B1.16.12): available (IAP.framework: IAPAudioSetVolume,
    IAPAudioIsVolumeControlSupported)` (the tag in that line is fixed in `Server.cpp`; B1.16.14 prints
    it too).
  - **NowPlaying 1.4.2** (`09a4d058`, B1.16.13) replaced 1.4.1 in the evening: the bundle directory was
    swapped (the old one is `/Applications/NowPlaying.app.pre-b11613`), without `uicache` and without a
    SpringBoard restart, and it ran at once.
  - **DockRemote B1.16.13** (`6206f014`) was loaded at 18:08:49 phone time, backup
    `DockRemote.dylib.pre-b11613.bak`, and logged the key passes of §2.5 (observe only). **DockRemote
    B1.16.15** (`66d34a6e`) replaced it and was loaded at 18:35:49, backup
    `DockRemote.dylib.pre-b11615.bak`; load line `DockRemote B1.16.15: hooked GSSendEvent with
    MSHookFunction from /usr/lib/libsubstrate.dylib`.
    - Each load was an iapd restart with the phone lifted off the dock by the owner. A script on the
      phone, `/var/mobile/fidelio/b1/redock.sh`, waits for the undock, restarts iapd only when the route
      is neither USB nor LineOut, and waits for the re-dock (§8).
  - **`remote-tweak.swallow`** was set at 18:41:17 on the owner's word ("yes turn on the takeover for
    spotify/airplay").
  - **B1.16.16** (59d03a1) went on between 19:28 and 19:32 phone time, from `b1/stage-b11616/`:
    audiod `9328a974` (backup `fidelio-audiod.pre-b11616.bak`, the B1.16.14 build), NowPlaying 1.4.3
    `bc8de26e` (the 1.4.2 bundle is `/Applications/NowPlaying.app.pre-b11616`), DockRemote `6a0acbd0`
    (backup `DockRemote.dylib.pre-b11616.bak`). Every file that had run before was **replaced by rename**
    (§8): the first attempt copied the new audiod over the installed file and launchd killed it at every
    start (§2.5). audiod B1.16.16 started at about 19:31:31 and logged `remote target (B1.16.16):
    /var/mobile/fidelio/remote-target ...`; it has run since without a restart. `redock.sh` was armed
    at 19:32:12; the owner lifted the phone at 19:34:11 (Spotify playing; the output moved to the
    speaker and back), iapd restarted at 19:34:19, the load line `DockRemote B1.16.16: hooked GSSendEvent
    with MSHookFunction from /usr/lib/libsubstrate.dylib (pass-through until the observe/swallow flags)`
    came at 19:34:20, the phone was docked again at 19:35:09–16 and `redock.log` ended with `DONE` at
    19:35:37. No reboot (boot time 16:49:17 that day).
  - Settings (unchanged since the morning): `dock_remote=on`, `wake_on_play=on` (was off),
    `wake_mode=launch` (the owner's choice), `lock_plugin=on`, `keep_screen_on=on`; `lockplugin.debug`
    present.
  - **iapd logs its packets** since the morning's restart, still on in the evening: `PrintIapPackets`, `LogDeviceVolumeChanges` and
    `LogLineOut` are set in `/var/mobile/Library/Preferences/com.apple.iapd.plist` (the file from before
    is `/var/mobile/fidelio/com.apple.iapd.plist.pre-r3`). The lines go to `/var/log/syslog`, which grows
    without rotation: every attach logs the dock's certificate. Remove the three keys when the volume
    work is done (undocked iapd restart).
  - After the install: the three jobs run, audiod got its AudioSession at the first attempt on the dock
    route (`USB`), Spotify played through it, and the wake brought NowPlaying up.
  - **The phone rebooted once that day**, about 20 s after a SpringBoard restart during playback
    (§2.5, §3.2). It came back by itself with everything running.
- **iPad** (installed by this session on the morning of 2026-11-22, on the owner's "Deploy to the dock";
  not touched in the evening): the whole suite as built with toolchain revision 3 at that time, so none
  of B1.16.12 … B1.16.15 (§2.3). Before B1.16.12's audiod or a later one goes on the iPad, run `voltest
  iap` there first (voltest is not installed there yet).
  - audiod `30eb78eb` (7330752, B1.16.10/11), lockctl `704c799f`, raop `baf304f6`, cspot `35de013e`
    (47d4760), NowPlaying 1.4.1 `35bfa229` (B1.16.9), `Fidelio.lockbundle` `e708fab0` (B1.16.7). Each
    sha256 was read back on the device.
  - What was there before is kept beside each file: `<name>.pre-r3.bak` in `/var/mobile/fidelio/b1/`,
    `/Applications/NowPlaying.app.pre-r3`, `Fidelio.lockbundle.pre-r3`. Before: audiod `c23cd624`
    (aeed5b1), lockctl `45da7833`, raop `9edce5e5`, cspot `2979aedd` (47d4760 was already installed),
    NowPlaying 1.4.0, lock bundle `559763a5`.
  - After the install: the three jobs run, audiod got its AudioSession at the first attempt (route
    `Speaker`: the iPad was not on the dock), both producers connected to it, and the iPad answers a
    direct Bonjour query with both service names. SpringBoard was restarted for the lock bundle.
  - **Nothing was played.** B1.16.9, B1.16.10 and B1.16.11 are installed there and not yet exercised
    (§3.2). The hashes were read back again on 2026-11-22 before the phone's install: unchanged.
  - Settings unchanged: `dock_remote=on`, `wake_on_play=on`, `unlock_on_play=off`, `lock_plugin=on`,
    `keep_screen_on=on`; `wake_mode` at its default, `launch`. No `remote-control.stage` file, so
    NowPlaying 1.4.1 runs stage 1.
- **Read-only survey of both devices** (2026-11-22, for the packages; nothing changed): §2.5, "What the
  packages rely on".

**After the migration (B1.17 packages; not yet installed on either device).** From
`packaging/README.md`, which has the full table:
- Programs, root-owned: `/usr/libexec/dock-receiver/` (`dock-audiod`, `dock-raop`, `cspot-ios3`,
  `cspot-run`, `keepalive`), `/usr/bin/dock-lockctl`, `/usr/sbin/dock-receiver-setup`.
- Jobs `com.dockreceiver.audiod`, `.raop`, `.cspot`, `.keepalive`: shipped in
  `/usr/share/dock-receiver/launchd/` and copied into `/Library/LaunchDaemons` by the postinst at the
  switch (so a failed install or a reboot mid-install cannot start two audiods). They run as `mobile`,
  with no options on the command line; their output goes to `/var/mobile/dockreceiver/logs/`.
- The app `/Applications/NowPlaying.app` (`com.dockreceiver.nowplaying`, 1.5.0); the lock plugin under
  both names, `/System/Library/SpringBoardPlugins/NowPlaying.bundle` and `NowPlaying.lockbundle`; on the
  phone, from the second package, `DockRemote.dylib`/`.plist` in
  `/Library/MobileSubstrate/DynamicLibraries/` as before.
- The data directory `/var/mobile/dockreceiver` (owner `mobile`), with the same file names as before
  (§9), plus `spotify-credentials.conf` (mobile, 600), optional `keepalive.conf`, `logs/` and
  `install.log`.
- `/var/dockreceiver-migration/` (root, 700): the old job plists, app, lock plugin and tweak, kept for
  `dock-receiver-setup rollback` until `dock-receiver-setup cleanup --yes`. `/var/mobile/fidelio` is
  copied from, never changed; it goes only with the cleanup.
- Licence material in `/usr/share/doc/dock-receiver/` (and `/usr/share/doc/dock-receiver-dockremote/`).

### 2.5 Device-proven facts (keep; don't re-test)
All of these were measured before B1.17, under the old names, and keep their recorded wording and the
log lines as the devices wrote them: `fidelio-audiod` is today's `dock-audiod` (likewise `-raop`,
`-lockctl`), `/var/mobile/fidelio` is `/var/mobile/dockreceiver`, `com.fidelio.*` is
`com.dockreceiver.*`, the lock plugin base name `Fidelio` is `NowPlaying`, `FidelioLockPluginController`
is `NowPlayingLockPluginController` (`RENAME.md` has the full list).

- **What the packages rely on** (read-only survey of both devices, 2026-11-22; nothing changed):
  - dpkg 1.18.10, architecture `iphoneos-arm`, on both; gzip, bzip2 and lzma, no xz; Cydia's `firmware`
    pseudo-package registered (3.1.3 on the phone, 5.1.1 on the iPad).
  - `/bin/sh` is bash 4.0.17 on both; `sed -r` works, `sed -E` does not.
  - The iPad lacks head, tail, tr, cut, sort, uniq, nohup, expr, stat, id, wc, basename, env, tee, du;
    the phone lacks ps; both lack awk, plutil, defaults.
  - The phone's MobileSubstrate is not registered with dpkg (it came with the firmware).
  - The phone has no `creds.env` (the Spotify credentials are inside its launcher scripts); the iPad has
    one.
  - The lock plugin is `Fidelio.bundle` on the phone and `Fidelio.lockbundle` on the iPad, beside
    Apple's `NowPlayingArtLockScreen.lockbundle`.
  - Free space on the root partition: 134 MB (phone), 249 MB (iPad).
- **iPad, unlock on play** (5.1.1, no passcode). With the four keys, `SBSOpenSensitiveURLAndUnlock(nowplaying:, true)`
  → true, and the iPad is unlocked with NowPlaying in front within 2 s (SpringBoard's `isLocked` read 0 at 2, 6
  and 12 s).
  - `SBSLaunchApplicationWithIdentifier` still answers **3** for seconds after that. Never use the launch as
    proof of an unlock.
  - Opening the app's own scheme while locked needs `com.apple.springboard.openurlswhenlocked`; `prefs:`
    does not.
- **iOS 3 (phone):** the wake's launch **succeeds over the lock screen** (NowPlaying covers the plugin), so
  audiod never sees "locked" there and `unlock_on_play` never triggers.
- **Lock plugin:**
  - SpringBoard creates it through the **class method `+rootViewController`**.
  - 5.1.1 ignores `-canScreenDim` for a plugin that is not full-screen.
  - Keep-lit works by restarting SpringBoard's dim timer once a second:
    - iPad: `-[SBAwayController restartDimTimer]` (no argument).
    - Phone: only `restartDimTimer:`, types `v12@0:4f8` (a float; 10 s is passed).
  - Traces: the phone layout is `view 320x480 … cover 140 at y 125, text to y 374`; the iPad's is `view
    768x1024 … tablet: cover 220 at y 430, text to y 798`.
  - After a **respring while playing**, the plugin is not shown again until the next start from nothing.
    B1.16.11 fixes this; not yet device-tested.
- **Dock keys:**
  - On the phone, iapd's C++ `SimpleRemoteEventHandler` never calls `-[SimpleRemote sendEventRecord:toApp:]`.
    iapd (thin armv6) imports `_GSSendEvent` and `_GSSendSystemEvent`.
  - On the iPad, iapd (thin armv7) imports no GSSendEvent and sends keys with **`MRMediaRemoteSendCommand`**
    to the now-playing app.
  - `MRMediaRemoteSetCanBeNowPlayingApplication` exists in 5.1.1's dyld shared cache.
  - Key numbers from iapd's own log: play/pause 0, next 3, prev 4, menu 22, select 23, up 24, down 25.
- **MobileSubstrate on the phone:** `/Library/MobileSubstrate/MobileSubstrate.dylib` is only the injection
  stub. `MSHook*` live in `/usr/lib/libsubstrate.dylib` (→ CydiaSubstrate).
- **Volume on the docked phone** (`voltest`, Spotify paused, `AudioIsPlayingSomewhere=0`). Every volume read
  0.000, which may only be the idle state; that is why a reading while playing was requested.
  - `route=USB` (Audio/Video), `routeHasVolumeControl=0`.
  - Audio/Video and MediaPlayback both map to the Audio/Video volume category; MediaPlayback's route is
    `LineOut`.
  - `CurrentlyActiveCategoryAttribute=MediaPlayback`, `HeadphoneVolumeLimitAttribute=1`.
- **cycript:** cycript `-p SpringBoard` crashed SpringBoard once. Use standalone cycript, or a probe binary like voltest.
- **Toolchain revision 3 on iOS 5.1.1** (iPad, 2026-11-22): binaries with the notice section load and run.
  - The smoke set (§3.2) gave exactly the expected output for all five programs, including the dylib and
    the bundle loaded with `dlopen`.
  - The suite's own revision 3 builds start and run there (§2.4).
- **Toolchain revision 3 on iPhone OS 3.1.3** (phone, 2026-11-22): the same smoke set gave exactly the
  expected output for all five programs, and the notice is in all seven files. The suite's revision 3
  builds run there and played Spotify through the dock (§2.4).
- **The dock and the phone's volume: what the packet log shows** (phone docked, 2026-11-22, iapd's
  `PrintIapPackets`; `Acc:` lines are the dock's, `iPod:` lines the phone's). This replaces the
  inferences of §3.1 where they differ.
  - The dock identifies the newer way (IDPS) and iapd then logs `setVolumeControlSupported - setting
    non-legacy volume support 1`. `voltest` shows `iap: supported=1` while docked and `supported=0` off
    the dock.
  - The dock **subscribes to the phone's volume**: Display Remote lingo (3), command 0x08, event mask
    `00000010`. The phone accepts it (ACK) and at once sends a notification, command 0x09 with
    `04 00 <volume>`: event 4, mute 0, volume 0 to 255.
  - The dock then **pushes its own level to the phone**: command 0x0E with `04 00 <volume> 01`. iapd logs
    `SetVolumeMaster - volume changing from A to B, ... notifyApp=1`, the system volume follows
    (`hwvolume: 'external'` in audiod), and the phone answers with the notification again. The dock does
    this on attach and for every press of its volume buttons, in steps of 4 (52, 56, 48 ...).
  - **Nothing on the phone sends the phone's volume to iapd.** `voltest set Audio/Video V` changes the
    system volume and iapd's volume stays where it was, idle and while Spotify plays. That is why the
    Spotify slider moves NowPlaying's display and not the dock.
  - **`IAPAudioSetVolume` does reach the dock's wire.** `voltest iap set V` gives `SetVolumeMaster ...
    notifyApp=0` and the notification `04 00 <V*255>` goes out to the dock (seen for 25, 76 and 47). The
    dock sends nothing back. The system volume does not follow (notifyApp=0). **The dock's loudness
    follows it:** the owner heard the dock go quieter, louder and back with those three calls
    (2026-11-22). B1.16.12 builds the fix on this (§3.1).
  - Digital Audio lingo (10): the dock offers 32000, 44100 and 48000 Hz; the phone sends its
    track-attributes message `0000ac44 00000000 00000000` (44100 Hz, sound check 0, volume offset 0) and
    the dock acknowledges it.
  - Readings: docked `route=USB`, `routeHasVolumeControl=0`, while playing too; off the dock
    `route=Speaker`, `routeHasVolumeControl=1`.
- **B1.16.12 on the phone: the dock follows the Spotify slider** (2026-11-22, audiod e590ef1 `7c04dc0b`,
  docked, Spotify in absolute mode).
  - The owner played Spotify on the dock, moved the Spotify slider, and reported that the dock's
    loudness follows.
  - The logs of that session agree. 33 slider sets logged `dock=ok` and 2 `dock=deferred`; the two held
    levels went out as `dock told ...`. All 35 calls are matched in iapd's log by `SetVolumeMaster ...
    notifyApp=0` at the same second with level*255 (within 1), and by the notification (lingo 3, command
    0x09) with that level. The dock sent nothing back after any of them. No failure line.
  - Nine presses of the dock's own volume buttons in the same session arrived as command 0x0E
    (notifyApp=1) and showed as five `external` lines in audiod: the 1 s poll merges fast presses.
  - Not tested in that session: AirPlay volume on the dock (tried later, next item), and the iPad. Whether the iPad's IAP.framework has the two
    functions is still unknown: run `voltest iap` there before installing this audiod on it.
- **AirPlay volume on the dock with B1.16.12** (phone, 2026-11-22, audiod `7c04dc0b`; the owner cast from
  an Android phone with a third-party AirPlay sender app, not sure which; it presents itself as
  `iTunes/12.12` and supports DACP). B1.16.14 is built to fix what went wrong (§3.1); it is installed on
  the phone and not yet tested with AirPlay.
  - Going up with the sender's control worked: the dock followed, 15 % to 42 % in four steps. The
    sender's own steps were 2 dB (AirPlay dB to level is linear, `db/30 + 1`). The first volume of a
    session is deliberately not applied (B1.13), so the sender starts from the dock's current level.
  - **The sender echoes the phone's level back.** raop reports each outside change to the sender over
    DACP (`dmcp.device-volume`); this sender accepts it and at once sends the same value back as an RTSP
    `SET_PARAMETER volume`. raop takes that as a user move and passes it to audiod.
  - **The dock jumped back up twice** when the owner turned it down with the dock's own buttons. An echo
    of an older report arrived after newer presses: audiod set the system volume back up and told the
    dock, 80 to 88 of 255 (undoing a press). A second echo was held by the 200 ms window
    (`dock=deferred`) and went out after a later press, 76 to 84: the rule "an outside change drops a held
    level" did not help, because the 1 s poll had not yet seen that press.
  - **The two ended out of step:** the last echo equalled audiod's level, which follows the phone's
    system volume, so it was skipped as "already there", and the dock stayed at 84 of 255 while the phone
    was at 76.
- **Dock keys as GraphicsServices events** (phone, DockRemote 4ed6a5a observing, 2026-11-22; two key
  passes, each compared with iapd's own log of the same presses: `SimpleRemoteEventHandler got evKeyDown
  ... key:N` and the dock's `Acc: lingo=0x02 ...` packets). B1.16.13 decodes with this (§10.5).
  - The tweak's assumed layout (key as `uint16` @52, a 0/1 state @56, down = 1) was wrong. In a
    type-4001 record with infoSize 8 the 32-bit word @56 is a **command code**; the half-word @52 was 0 in
    every event; the header words were the same for every key.
  - The **flags word** tells the two events of a press apart: `0x400` on the first (down), `0x800` on the
    second (up). Both carry the same code.
  - A **type-101** event (flags 0, infoSize 8, info all zero) comes before every press and every dock
    volume step. It must always pass through.
  - **Codes:** play/pause 0 (two events, `0x400` then `0x800`); menu 22, select 23, up 24, down 25 (two
    events each; these equal iapd's own key numbers 22 … 25).
  - **down** is verified (2026-11-22, B1.16.13 on the phone): dock packet `00000002`, iapd key 25, events
    (`0x400`, 25) and (`0x800`, 25), three presses.
  - **right = next** (dock packet `08`, iapd key 3): ONE event, flags `0x800`, in two forms, both
    measured: code 20 on the first press of a run, code 3 on the quick repeats after it (18:40:57–59: 20,
    3, 3; B1.16.15 observing). Code 3 was assumed by symmetry when B1.16.15 was built; it is measured
    now. B1.16.15 treats 20 and 3 as next.
  - **left = previous** (dock packet `10`, iapd key 4): ONE event, flags `0x800`, in two forms, measured:
    code 21 once (the first observation of the day), and code 4 on every press since (17:04:21, 18:11:33,
    18:12:25 and all later ones), each compared with iapd's packet log. Code 4 had also come once in the
    second observe pass, when a press's packet reached iapd twice. The single 21 fits next's pattern
    (first press of a run 20/21, repeats 3/4), but it is one observation; why iapd picks one form or the
    other is not established. The earlier guess that a hold gives code 4 is not confirmed. B1.16.15
    treats 21 and 4 as previous.
  - iapd logs `stoppting timer` (sic) after the releases of next and previous only.
  - **Presses the dock did not send:** up (packet `00000001`) came in one pass (code 24, two events) and
    not at all in the next pass, although the owner pressed it many times; some select presses were not
    sent either. iapd's packet log has no packet for them, so nothing in the tweak can change that. In the
    first observe pass four keys did not arrive at all, for the same reason. The evening's figures are
    below.
  - With no swallow, play/pause still reaches the built-in iPod app, as expected.
- **Dock keys, the evening of 2026-11-22** (phone, DockRemote B1.16.13 from 18:08:49, then B1.16.15 from
  18:35:49, observing; the owner pressed keys and watched NowPlaying 1.4.2's "Dock remote" section; the
  session compared the dock's packets, iapd's `evKeyDown` lines and `remote-keys.log`):
  - **Every packet the dock sent was handled at every stage:** 104 of 104 between 18:10 and 18:26, 58 of
    58 between 18:26 and 18:38 (dock packet, iapd `evKeyDown`, a line in `remote-keys.log`).
  - **The phone sends the dock nothing after the first 15 seconds of an attach:** no packet when the
    front app changed, or when the built-in iPod app (the owner calls it the Music app) started, played or
    paused. So nothing the phone reports can make the dock filter keys.
  - All type-4001 events went to one receiving port, whichever app was in front. Only the built-in iPod
    app acts on up, down, select and menu: the owner saw nothing move in Settings; in the iPod app up and
    down moved the selection and menu went back to the song list.
  - **The up key arrives unreliably:** 5 ups against 32 downs, 20 selects and 20 menus between 18:10 and
    18:26 (0 ups against 16 downs while Settings was in front), yet runs of 10 and 7 ups arrived around
    18:30–18:31. The loss is before the phone (the remote's key or the dock); the owner doubts the
    remote's hardware.
  - **Rapid repeats are thinned out before the phone, and a hold is not passed on as a hold.** The owner
    pressed up ×1, down ×2, left ×3, right ×4 and then held OK (18:42:00–18:42:24); the dock sent 1, 1, 2
    and 2 packets for the four keys and, for the hold, two ordinary press/release pairs a second apart.
    Presses about a second apart all arrive (runs of 3, 7 and 10).
  - The status bar's play icon shows only when the built-in iPod app plays, not for the project's sources
    (the owner's observation). Whether a daemon can raise it on iPhone OS 3 is a backlog idea (§3.4).
- **The takeover, first device test** (phone, 2026-11-22 18:42, DockRemote B1.16.15 with
  `remote-tweak.swallow`, `dock_remote=on`, Spotify active):
  - Two previous presses (code 4) and two next presses (code 3) were logged `taken`. The tweak logged
    `TAKEN code=4 flags=0x800 -> audiod verb=5 (sent)` and `TAKEN code=3 ... verb=4 (sent)`; iapd's syslog
    has `DockRemote: forward verb N to audiod -> sent`; audiod logged `control: prev -> 'spotify'` and
    `control: next -> 'spotify'`.
  - Up, down and select were `passed`.
  - audiod logged no `audio interruption`, so the built-in iPod app did not take the audio.
  - Not yet reported: what the owner heard (whether Spotify skipped). Not yet tested under the takeover:
    play/pause; AirPlay as the active source; the keys with no source active (they must pass to the iPod
    app).
  - **Side effect in audiod's log:** after each skip the stream was silent for longer than the release
    time; audiod logged `release: 'spotify' -> 'none' (quiet 1504 ms)` and about 2 s later `takeover:
    'none' -> 'spotify'`, and that takeover ran wake on play again (NowPlaying launched, the lock plugin
    enabled again). A track change that is silent for longer than `release_ms` counts as a new start
    from nothing. This is not specific to the remote: any skip with a long enough gap does it. Open
    (§3.3, §4).
- **The takeover covered a playing source, not a paused one** (phone, 2026-11-22, DockRemote B1.16.15 with
  `remote-tweak.swallow`, `dock_remote=on`, Spotify playing). B1.16.16 is built on this (§3.2, §10.5).
  - 18:51:32 play/pause: the tweak took it (`TAKEN code=0 flags=0x400 -> audiod verb=3 (sent)`), audiod
    logged `control: toggle -> 'spotify'`, Spotify paused. Correct.
  - 18:51:34 audiod logged `release: 'spotify' -> 'none' (quiet 1516 ms)` and `active-source` became
    `none`; 18:51:39 `idle-stop`.
  - 18:51:41 play/pause again, to resume: the tweak's condition (a source of ours active, read from
    `active-source`) no longer held, so the key passed to the system and the built-in iPod (Music) app
    started playing (audiod: `audio interruption began (was idle)`). The owner: "play pause worked to
    pause spotify and then iphone music app resumed."
  - So audiod's interruption callback does fire while it is idle, when the Music app starts.
- **B1.16.16 on the phone: the remote keeps a paused source** (2026-11-22, 19:35–19:37 phone time; audiod
  `9328a974`, DockRemote `6a0acbd0`, NowPlaying 1.4.3, `remote-tweak.swallow` set, `dock_remote=on`).
  - **By eye and ear (the owner):** "All looks good. Paused and resumed Spotify and once switched to music
    remote worked on music. Spotify let go so paused state music didn't restart Spotify even when
    switching back to NowPlaying."
  - **The logs** (audiod's log and event log, `remote-keys.log`, `remote-events.log`, iapd's syslog lines,
    cspot's log; the remote's play/pause is key 0, verb 3 = TOGGLE):
    - 19:32:34 Spotify started: `takeover: 'none' -> 'spotify'`, `remote target: 'none' -> 'spotify'
      (active)`.
    - 19:35:26 pause: iapd `evKeyDown ... key:0`, `DockRemote: forward verb 3 to audiod -> sent`, key log
      `0 play-pause taken`, audiod `control: toggle -> 'spotify'`, cspot `control: toggle (pause)`.
      19:35:28 `release: 'spotify' -> 'none' (quiet 1510 ms)`, 19:35:33 `idle-stop`; no `remote target`
      line, so the target **stayed `spotify`**.
    - 19:35:40 resume: `taken`, audiod `control: play -> 'spotify' (resume) (paused remote target)`, cspot
      `control: play`, and in the same second `takeover: 'none' -> 'spotify'`. No `audio interruption`;
      the Music app did not start.
    - 19:35:51 pause again: `taken`, `control: toggle -> 'spotify'`, 19:35:52 `release` (quiet 1512 ms),
      target still `spotify`.
    - 19:35:58 the owner switched from NowPlaying to the Music app (SpringBoard's log), and at about
      19:36:06 audiod logged `audio interruption began (was idle)` and, as its next line, `remote target:
      'spotify' -> 'none' (other audio is playing (not ours): the remote's keys go back to the system)`.
      **AudioSession's `OtherAudioIsPlaying` does report the built-in Music app** while audiod is idle
      (its session inactive). The interruption's time is derived from audiod's later line
      `interruption began 48831 ms ago` at 19:36:55, within a second. audiod's lines carry no time of
      their own, so the delay from the interruption to the clear is not measured; it is bounded by the
      500 ms poll, and nothing else was logged between the two lines. The 8 s between the app switch
      and the interruption include the Music app's start and the owner starting playback by hand.
    - 19:36:22, 19:36:40 and 19:36:50 play/pause: `passed` (no forward line in iapd's log), to the Music
      app; at 19:36:29 the owner switched back to NowPlaying (relaunched at 19:36:31, as every app switch
      on iPhone OS 3 does), and the presses still passed and did not restart Spotify.
    - 19:36:55 Spotify played again: cspot logged `External play command` (from the Spotify controller,
      with no `control:` line and no key press before it), audiod `takeover: 'none' -> 'spotify'`,
      `remote target: 'none' -> 'spotify' (active)`, and `feed: interruption began 48831 ms ago and never
      ended, no other audio playing: clearing it` (the Music app was paused by then, and its interruption
      never ended).
  - Counted from 19:31 to 19:40: 3 presses taken, 3 passed, each as expected; one `other audio` clear,
    at the Music app's start, so no false positive; no `send failed`; one audiod start (B1.16.16), no
    restart of audiod, raop or cspot; no reboot.
  - **The resume ran wake on play:** at 19:35:40 the `takeover: 'none' -> 'spotify'` after the resume was
    followed by `wake: 'none' -> 'spotify' (wake+launch com.fidelio.nowplaying)` (launch -> 0, 0 ms;
    NowPlaying was in front already) and `lock-plugin enabled Fidelio`. Each pause's release logged
    `lock-plugin disabled Fidelio`. The same happened at 19:36:55. This is §4 question 6.
  - Not tested yet: next and previous while paused; AirPlay as the paused target; the end of the Spotify
    session while paused; B1.16.14's AirPlay echo fix; the keys with no source of ours ever active; the
    iPad (§3.2).
- **Replace a file that has run by rename, never by writing into it** (phone, iPhone OS 3.1.3,
  2026-11-22, the B1.16.16 install). The first attempt copied the new audiod over the installed file
  (`cp`, the same inode). launchd then logged `(com.fidelio.audiod) Exited: Killed` and `Throttling
  respawn: Will start in 3 seconds` 8 times between 19:29:14 and 19:29:36; the binary wrote no log line,
  and the system log had no code-signing or AMFI line. The same file run from the stage directory
  printed its `--help`. The old content copied back over the same inode ran again. Installed by rename
  (copy to `fidelio-audiod.new`, owner and mode like the old file, sha256 checked, `mv -f` over the path)
  it started normally. As far as measured: the kernel keeps what it learned of a file that has run (its
  code signature) with the inode, so new content in the same inode is killed. DockRemote.dylib, mapped
  by the running iapd, was replaced the same way. The install procedure is in §8.
- **A SpringBoard restart during playback rebooted the phone** (2026-11-22, once): `killall SpringBoard`
  while Spotify played on the dock; SpringBoard came up and loaded its tweaks, and about 20 s later the
  phone restarted from the kernel up, with no line in the system log in between, no panic log, and the
  flash layer recovering as after an unclean stop. The same restart 10 minutes earlier, with nothing
  playing and the tweak of that day not yet loaded, was harmless. The cause is not established. What was
  new in that session: audiod 7330752 (which re-enables the lock plugin after a SpringBoard restart),
  DockRemote 4ed6a5a hooking `GSSendEvent` in iapd, and iapd's packet logging. **Do not restart
  SpringBoard on a playing device again without the owner's word.**
  - Two of those are unlikely on the evidence. audiod's re-enable did not run: it had unlocked the
    phone for that start, so the plugin was not enabled and `syncLockPlugin` returns before it looks at
    SpringBoard (`Server.cpp`); audiod's log has no line from that window. And the tweak's log has no
    event between the last key press and the reboot, so the hook was not called in that window.
  - Also running at that moment: the built-in iPod app (the remote's play/pause had started it a few
    minutes before; SpringBoard's exit terminated it, NowPlaying and the Phone app), and audiod
    streaming to the dock over USB.
- **Wake on play on the phone** (2026-11-22, audiod 7330752, `wake_mode=launch`, no `unlock_on_play` line
  so the default "on"): the event log read `wake: device locked, unlock accepted (nowplaying:)` and
  NowPlaying was running afterwards. The older note below ("audiod never sees locked there") was written
  for an earlier build; what the owner saw on the screen is not reported.
- **Bonjour on the iPad** (2026-11-22, one session, from the PC over Wi-Fi): the iPad answered a DNS-SD
  query sent directly to it (both service names), and did not answer the same query sent to the
  multicast address in four tries, while three other devices on the network did. This bears on the
  keep-warm idea of §3.4. Not investigated further.

---

## 3. Open work, in order

### 3.0 B1.17 on the devices: the migration (first; built, not yet run on a device)
B1.17 (§2.1, §2.2) is built and checked on the host; nothing of it has run on a device, and both devices
still run the old setup (§2.4). In order:
1. **The independent review of the install scripts** (at `1f7d18d`) took place. It found three must-fix
   problems (a failed install followed by a rollback could strip the old app, an undo or rollback could
   start the old audiod beside a dock-audiod that would not stop, and a failed re-run left the new jobs
   unloaded) and a number of smaller ones; together with the facts of the devices' first dry run (the
   links into `/var/stash`, the old Spotify job's chain of files) they were fixed at `c8b1475`, each with
   host tests that fail against `1f7d18d`; the package hashes of §2.3.1 are those of `c8b1475`.
2. **`main`** is fast-forwarded to `b117-rename` and pushed, with the marker branch `audiod-b117` (§2.1,
   §7).
3. **With the owner at the dock**, on each device:
   1. the dry run: `packaging/install.sh --debs <dir> --check root@<device>` (changes nothing; prints
      what it found, where the Spotify credentials were found but never their values, and `result:
      ready` or `NOT ready`);
   2. the install: `packaging/install.sh --debs <dir> root@<device>` (the phone gets
      `dock-receiver-dockremote` too);
   3. only on the owner's word and with nothing playing: the SpringBoard restart,
      `dock-receiver-setup respring` (the install never does it by itself; §2.5, the reboot after a
      respring during playback);
   4. on the phone: the lift off the dock for the tweak (undocked `killall iapd`, then dock again; the
      packages never restart iapd);
   5. verify: the install's summary and `/var/mobile/dockreceiver/install.log` (`packaging/README.md`,
      "What the install does"); the jobs `com.dockreceiver.*` loaded and the old ones not; one
      `dock-audiod` and no `fidelio-audiod` (`killall -0`); `active-source` written; NowPlaying 1.5.0
      not showing "Daemons not running"; Spotify (the credentials carried over) and AirPlay play; the
      lock plugin after the respring; on the phone the load line `DockRemote B1.17: hooked ...` in
      `remote-events.log` and the remote's keys.
   - Rollback: `dock-receiver-setup rollback` (removes both packages; the old setup comes back from
     `/var/dockreceiver-migration`). `dock-receiver-setup cleanup --yes` only when the owner is
     satisfied: after it no rollback is possible.
4. **Only a device can confirm** (the host suites run the scripts under GNU bash 4.0.0 with stand-ins):
   - that the old dpkg reads the hand-assembled archives;
   - `dpkg -s` from inside a maintainer script;
   - the output formats of `launchctl list`, `killall -0` and `df` on both OS versions;
   - `sed -r` with the `I` flag on sed 4.1.5, and `grep -o` on grep 2.5.4;
   - bash 4.0.17 against the 4.0.0 of the tests;
   - whether launchd opens the job logs in append mode (the log trimming relies on it);
   - the UDP keepalive;
   - whether `uicache` alone picks up the new app id;
   - whether each OS ignores the other's plugin bundle name (both are installed);
   - the real form of the credentials in the phone's launchers (the dry run shows where they were found,
     never the values).
- **Still owed from before B1.17** (unchanged by it; they can run on the migrated setup, the owner
  decides the order): the AirPlay tests on the phone (B1.16.14's volume echo fix, §3.1; AirPlay as the
  paused remote target, next/previous while paused, the no-source case, §3.2); everything on the iPad
  since the morning of 2026-11-22 (§3.2); the status-bar play icon idea (§3.4); the cause of the phone's
  reboot after a respring during playback (§2.5, §3.2 item 2); removing the three iapd logging keys on
  the phone (§2.4).

### 3.1 B1.16.12: the phone's volume must move the dock (phone PASS for Spotify; AirPlay reaches the dock, its echo fix B1.16.14 installed on the phone and not yet tested with AirPlay; iPad and the relabel open)
- **Report:** phone docked, `spotify_volume=absolute`. The Spotify slider moves NowPlaying's display and
  audiod logs `hwvolume: 'spotify' -> X (set X by 'spotify' slider Y)`, but the loudness doesn't change.
  The dock buttons work and show up as `hwvolume: 'external'`.
- **State on 2026-11-22, evening: the fix passed its device test on the phone** (§2.5). The owner's
  requirement (absolute volume on this dock from the phone) is met for Spotify on the phone.
  - The cause (§2.5): nothing on the phone forwards the system volume to iapd, so the dock never hears
    of a change made on the phone. The owner confirmed by ear on 2026-11-22 that the dock follows
    `IAPAudioSetVolume`: quieter, louder and back with `voltest iap set`.
  - The fix (e590ef1 on `main`, marker `audiod-b11612`, audiod only; §10.4): in absolute mode, after a
    source's set of the system volume (the Spotify slider, the AirPlay volume), audiod calls
    `IAPAudioSetVolume` with the same level, when `IAPAudioIsVolumeControlSupported` says yes at that
    moment. Never for an external change (the dock's buttons, another app). A drag is coalesced: one call
    per 200 ms at most, the latest level last. The `hwvolume` line ends `; dock=ok` when the dock was told.
  - The device test on the phone: the loudness follows the slider; 35 calls, every one matched in iapd's
    log; the dock's buttons still give `external` lines with no `dock=` (§2.5).
  - **AirPlay absolute volume reaches the dock** (phone, 2026-11-22, §2.5): the dock followed a
    third-party sender's control. With that sender the dock's own buttons then made the dock jump back
    up and left dock and phone out of step: the sender echoes every level raop reports to it back as a
    volume command, and audiod applied a late echo as a user move and told the dock.
  - **B1.16.14** (23f407d on `main`, marker `audiod-b11614`, audiod only, §10.4) is built to fix that.
    It is installed on the phone since the evening of 2026-11-22 (§2.4); Spotify played through it
    during the remote tests without a problem. It has not been tested with AirPlay yet.
    - audiod keeps the last 8 levels it reported to each source (WELCOME, HWVOLUME) with their times. A
      slider set from that source within 3 s of a report and within one dock step (1/255) of the
      reported level is its echo: no set, no dock call, no new report, one trace line.
    - A held dock level is dropped at its flush if the hardware level is no longer the one audiod set
      (read again at that moment), even when the poll has not yet seen the change.
    - Accepted cost: a genuine move on the sender that lands within one dock step of a level reported in
      the last 3 s is ignored.
    - **Device test still owed** (B1.16.14 is installed): the same Android sender, up with its control,
      down with the dock's buttons; the dock must not jump back, and the phone's and the dock's levels
      must end equal (iapd's log). In audiod's log, look for `echoed ...: ignored` lines, and for `held
      dock level ... dropped ... not yet reported` lines that have no real button press behind them
      (that would be a false drop).
  - **What cannot be changed from audiod:** a third-party sender's own scale and steps (here 2 dB), and
    what it does with a DACP report. An echo that arrives more than 3 s late, or more than one dock step
    off, is still applied. A dock press that lands between audiod's set of the system volume and its
    call to iapd (the same run-loop pass, two MIG round trips) can still leave the dock at the source's
    level and the phone at the press's level until the next change.
  - **AirPlay volume mode, an option the owner knows of (2026-11-22, not a decision):** the owner
    suggested switching AirPlay to the proportional mode and back to re-align the levels. The session's
    answer: nothing is stuck; the offset at the start of a session is by design (the first volume of a
    session is not applied, B1.13); and "Software gain" for AirPlay alone (`airplay_volume_mode` is per
    source; Spotify stays absolute) may suit a third-party sender with its own coarse scale. Nothing was
    changed.
  - **Still open:**
    - The relabel of the settings below.
    - The device test of B1.16.14 with the AirPlay sender (installed on the phone; above).
    - The iPad: run `voltest iap` there first (does its IAP.framework have the two functions? voltest
      has to be copied there), then install this audiod and check the start-up line.

The record of the investigation follows.
- **The owner's requirement (2026-11-18): absolute volume on this dock unless it is absolutely not
  possible.** The phone's volume setting must change the dock's loudness.
  - An "Auto" mode that silently falls back to software gain does not meet the request and is not the
    plan. (The earlier three-way "Auto" design is in the history file, Task 36. Do not build it.)
  - Software gain stays a manual choice and the last resort.
  - The owner is not convinced by the paused reading below: "maybe a replug or a different output mode
    affects it".
- **Verified so far:**
  - In the code: audiod already sets the system volume. `HardwareVolumeApple.cpp` calls
    `-[AVSystemController setVolumeTo:forCategory:@"Audio/Video"]`, and the call returns YES.
  - In the code: this and `Server::applySlider` / `sliderToLevel` are **unchanged since B1.9/B1.10**.
  - On the device: one `voltest` reading on the docked phone, with Spotify **paused** (§2.5). It showed
    `route=USB` and `routeHasVolumeControl=0`. Taken at face value, the system volume has nothing to scale
    on that route and the dock amp is the master. It is one paused reading, not a conclusion.
- **Where "the dock followed" was confirmed for B1.9/B1.10 is not recorded.** The history file's Task 35
  says the code is "unchanged since B1.9/B1.10, when you confirmed the dock followed". The tasks
  themselves say:
  - Task 12 (B1.9) only lists what to check "On the phone": "move the Spotify slider -> `hwvolume
    spotify ... set` and the dock level follows". It calls this "Not verifiable off-device: that
    `setVolumeTo:forCategory:` from a daemon moves the dock level".
  - Task 13 (B1.10) is titled "bug-fix round from hardware testing of B1.9 on the iPad". It does not say
    that the dock's loudness followed, on either device.
  - Later notes: Task 15 has "B1.10 (b4ba2ff) confirmed on the iPhone (5 button changes during AirPlay
    caused no takeover; moving the Spotify slider during AirPlay set the hardware level and the user wants
    that kept); B1.9 absolute volume confirmed working". Task 16 has "B1.9 absolute (Spotify) confirmed".
  - So the record names the iPhone for B1.10's check that the slider "set the hardware level". It does not
    say on which device, or on which output, the dock's loudness was heard to follow. The earlier
    handoff's "B1.9/B1.10 must have run on a route with volume control" was an inference.
- **State on 2026-11-22, after the first packet log** (the facts are in §2.5):
  - The dock asks for the phone's volume and the phone agrees to send it. The dock is not refused, and
    no hook in iapd is needed for the subscription.
  - Nothing on the phone forwards the system volume to iapd, so the dock never hears of a change made
    on the phone. `IAPAudioSetVolume` does send it.
  - **One thing is open, and it decides the fix: does the dock's amplifier follow the notification?**
    On 2026-11-22 three notifications went out while Spotify played (25, 76, then 47 of 255, at 16:47:35,
    :42 and :50 phone time). Answered the same day: the loudness followed (the state above).
    - If it follows: audiod calls `IAPAudioSetVolume` whenever it sets the system volume (item 4 below).
      That is the whole fix, and absolute volume works on this dock.
    - If it does not: try the Digital Audio offset (step 5 below), then look at what the dock does
      with the notification (it may only show it).
  - Of the five readings below, 1 and 5 are done (§2.5); 2 is answered by the packet log (the dock's
    buttons set the phone's volume with command 0x0E, so the system volume follows them); 3 is done for
    `Audio/Video` (no forwarding); 4 is not done.
- **The work is an investigation first:**
  1. **The five readings still owed** (asked 2026-11-15):
     1. `voltest` while **playing**.
     2. `voltest watch 20` while pressing the dock buttons (does Audio/Video move?).
     3. `voltest set Audio/Video|active|MediaPlayback 0.15`, each by ear.
     4. Spotify volume = "Software gain": does the slider then change the dock? Expected: yes.
     5. One undocked reading (`routeHasVolumeControl` should be 1).

     Now also: note whether the phone's own volume buttons and the built-in iPod app's volume slider move
     the dock, and repeat a reading after reseating the phone.
  2. **Static analysis, done 2026-11-18** (no device touched): the phone's `iapd` (build iapd-444.1) and
     the 3.1.2 SDK's IAP, Celestial and CoreMedia. The full notes are on the owner's PC at
     `C:\code\fidelio\research\2026-11-18-phone-dock-volume.md`.
     - **What audiod's call does.** `setVolumeTo:forCategory:` ends in CoreMedia's session manager, which
       stores the level and writes it to the virtual audio device. Celestial and CoreMedia never
       reference IAP. `currentRouteHasVolumeControl` reads a property of that audio device.
     - **The one path from the phone's volume to an accessory in this iapd.** `IAPAudioSetVolume(float)`
       (the private IAP.framework; a MIG call to iapd) reaches `ISL::MediaVolumeMgr`'s setter with
       notifyApp = 0. That raises an internal "volume changed" event, and the Display Remote lingo then
       sends the accessory a volume notification, **but only if the accessory asked for volume events**.
       Line-out and mute do not gate it.
     - **Who calls `IAPAudioSetVolume`:** no binary in the SDK. If anything does, it is the virtual audio
       plug-in inside `mediaserverd`, which was not analysed.
     - **Accessory → phone:** the Display Remote "set state" command and the Simple Remote volume steps
       reach the same setter with notifyApp = 1, which calls `SBSetMediaVolume`. Inference: the dock's
       buttons use the former.
     - **"Volume control supported" is two flags.** One comes from a bit in the capability mask an
       accessory sends when it identifies the newer way (IDPS). The other is raised when an accessory
       that identified the older way subscribes to, or asks for, the volume. An IDPS accessory without
       the capability bit is refused the subscription. The second flag is cleared on detach and when
       the device sleeps; only a new request from the accessory raises it again.
     - **A second channel exists:** `IAPDigitalAudioSetVolumeOffset(int)` puts a value into the Digital
       Audio lingo's track attributes. Its units, and whether this dock applies it, are unknown.
     - **Re-checked by the session against the binaries:** the IAP exports; `IAPAudioSetVolume`'s
       argument and return value; iapd's routine for it (clamp to 0..1, scale by 255, notifyApp = 0).
     - **The most likely reading (inference):** the dock pushes its own level to the phone but has no
       accepted subscription to the phone's volume on this OS. One packet log tells three cases apart:
       the dock never asks; it asks and is refused (a hook in our tweak can lift that); or it subscribed
       and nothing forwards the phone's volume (audiod calling `IAPAudioSetVolume` fixes that).

     The history file's Task 12 lists `SBSetMediaVolume(port, float)` / `SBGetMediaVolume`
     (SpringBoardServices) as "confirmed present, **not used** (noted as the alternative volume path if
     AVSystemController misbehaves)".
     - **The Digital Audio lingo's volume field** (follow-up analysis, 2026-11-18; the owner asked for
       this path to be tested too). The audio stream is USB audio with the dock as the host, so a
       volume request inside the stream itself goes from the dock to the phone, not the other way; the
       phone → dock direction has to use the accessory protocol beside it. (The host and device roles
       are from general knowledge of these docks, not yet read off this phone.)
       - iapd treats the value of `IAPDigitalAudioSetVolumeOffset` as an opaque 32-bit word and puts it
         unchanged into the third field of the lingo's track-attributes message, next to the sample
         rate and a sound-check field that is always 0 in this build.
       - The message goes out at once when the value changes, if the lingo is active. iapd re-sends it
         every 500 ms until the dock acknowledges it. So the packet log shows whether the dock accepts
         the message; only listening shows whether it applies the field.
       - Units: upstream, MediaToolbox and CoreMedia compute a "hardware volume offset" in decibels
         (−127 to +18, from the item's volume and Sound Check) for the audio device. That this is what
         reaches iapd is inferred from the name only; the connecting code is in `mediaserverd`.
       - A change of the phone's volume may overwrite a test value with the system's own.
  3. **Next, at the dock, in this order** (`voltest` from b8d8976 or later; every state line now ends
     with `iap: supported= volume= lineout= digital=`):
     1. **Turn on iapd's logging first,** so every later step shows in the syslog. Undocked: set the
        booleans `PrintIapPackets`, `LogDeviceVolumeChanges` and `LogLineOut` in iapd's preferences
        (domain `com.apple.iapd`), restart iapd **undocked** (the keys are read once at start), then
        dock. In the log, `Acc:` lines come from the dock and `iPod:` lines are sent by the phone.
     2. **Read the attach:** how the dock identifies and its capability mask; whether it subscribes to
        volume events on the Display Remote lingo, and the phone's answer; the Digital Audio exchange
        (the phone's track-attributes message and the dock's acknowledgement).
     3. **While playing:** `voltest`, then `voltest set Audio/Video 0.15`. Does `iap: volume=` follow? If
        it does, the system forwards the phone's volume to iapd.
     4. **The notification path:** `voltest iap set 0.15`, then `0.30`. Does the dock's loudness follow?
        The log shows whether a volume notification went out.
     5. **The Digital Audio path:** `voltest iap offset N`, without touching the phone's volume.
        - N in this order: −12, −30, 0 (decibels as an integer). If accepted but inaudible: −1200,
          −3000, 0. Then the float bits of −20.0 (−1046478848), then 0.
        - Reading it: `-> 0` means nothing was sent (lingo inactive). A track-attributes message
          followed by the dock's acknowledgement means accepted: listen. The message repeating twice a
          second means the dock ignores or rejects it. A later message ending in zeros that was not
          requested means the system overwrote the value.
        - Avoid large positive values. Finish with 0: the value stays in iapd until undock.
     6. **The dock's buttons:** which command they send, and the `volume changing … notifyApp=` lines.
     7. **The USB roles:** read the phone's USB configuration while docked, to confirm the dock is the
        host.
     8. Hooks from the DockRemote tweak, one at a time, chosen by the packet log.
     9. The same packet log on the iPad as the working case to compare with, if absolute volume does
        work there.
  4. **Then the fix follows from what the dock does:** audiod calls `IAPAudioSetVolume` after it sets the
     system volume; or the DockRemote tweak, which already runs inside iapd on the phone, makes iapd
     accept or force the dock's subscription; or both.
- **The settings stay as they are for now:** `spotify_volume` and `airplay_volume_mode`, absolute |
  software, default absolute, per source. The relabel of the choices is still wanted: "System volume (the
  dock follows)" and "Software gain (scales the stream)".
- **Workaround meanwhile:** Settings → Volume → Spotify volume → "Software gain" on the phone.

### 3.2 Device tests queued (on the owner's go-ahead)
Hands-on work waits until the owner is at the dock. The volume readings of §3.1 are queued as well.

**The start-up smoke test for toolchain revision 3: PASS on both devices** (iPad and phone,
2026-11-22; §2.5). Seven small files linked with revision 3 (a C, a C++ on libstdc++, a C++ on libc++
and an Objective-C program, and a loader with a dylib and a bundle); they are in WSL at
`/home/user/smoke-r3/` and on the PC at `C:\code\fidelio\cloud-build\smoke-r3\`, with the commands
and the expected output in `README.txt`. It shows that the loader of each OS accepts a binary with the
extra section and that start-up and lazy binding work as before.

1. Install audiod **7330752** on both devices. Set `wake_on_play=on`, and `wake_mode=launch` on the phone (the
   owner's choice).
   - Done on both on 2026-11-22 with the revision 3 builds (§2.4).
2. **B1.16.11 check:** with the plugin on and music playing, respring. Within ~6 s the event log should
   show `lock-plugin re-enabled Fidelio after a SpringBoard restart` (`... NowPlaying ...` once the
   device runs B1.17). Also flip "Lock-screen plugin" off
   and on while playing.
   - **Phone, 2026-11-22: not passed, and not safe to repeat as it is.** The respring during playback
     ended in a reboot of the phone about 20 s later (§2.5); the line was never logged. Find the cause
     first: the owner decides whether and how to reproduce it (one variable at a time: the tweak
     disabled with `remote-tweak.disabled`, the lock plugin off, nothing playing).
   - iPad: open. Ask the owner before doing it there, given the above.
3. **DockRemote** (phone).
   - The B1.16.8 observe run is done (2026-11-22, two passes): it gave the decode of §2.5. B1.16.13 is
     built on it.
   - B1.16.13 was installed (DockRemote and NowPlaying 1.4.2) and logged the key passes of §2.5.
   - **B1.16.15 is installed** (2026-11-22, loaded 18:35:49, §2.4). Observing, left came as code 4 and
     right as 20 then 3, logged as "previous" and "next" (§2.5).
   - **The swallow test, first part done** (18:42, Spotify active; §2.5): two previous and two next
     presses were taken and reached Spotify through audiod as PREV and NEXT; up, down and select passed;
     no audio interruption. `remote-tweak.swallow` stays set.
   - **Still to do under the takeover:**
     - Ask the owner what was heard: did Spotify skip back and forward?
     - play/pause with Spotify active: it should pause and resume Spotify, show "taken", and the iPod app
       must not start.
     - The same keys with AirPlay as the active source.
     - The same keys with no source active: they must show "passed" and reach the iPod app as before.
     - **play/pause tested 18:51 (§2.5): it paused Spotify, and the resume press passed to the iPod app.**
       B1.16.16 fixes that (installed and passed, below).
   - **B1.16.16 install and test** (on the owner's go-ahead): install `fidelio-audiod` (restart the
     daemon), `DockRemote.dylib` (undocked, `redock.sh`), both by rename (§8), and NowPlaying 1.4.3
     (swap the bundle directory), from `cloud-build/deploy-b11616` (`SHA256SUMS` there). Check the start-up line `remote
     target (B1.16.16): /var/mobile/fidelio/remote-target ...` and the load line `DockRemote B1.16.16:
     hooked ...`. Then, with Spotify playing on the dock:
     - play/pause: Spotify pauses, "taken"; wait more than 2 s (the release; `remote-target` stays
       `spotify`, NowPlaying keeps the cover and shows "Paused via Spotify"); play/pause: Spotify
       resumes, "taken", no `audio interruption` in audiod's log, the iPod app does not start.
     - Paused: next and previous: "taken"; what Spotify does (cspot moves the queue and, from its code,
       stays paused) is to be reported.
     - Paused, then start the Music app by hand: audiod logs `remote target: 'spotify' -> 'none' (other
       audio is playing ...)`, NowPlaying goes to "Idle", and play/pause passes to the Music app; after
       the Music app is paused the keys still pass, until Spotify plays again.
     - Paused, then end the Spotify session (disconnect in the Spotify app): `remote-target` `none`.
     - The same with AirPlay (pause from the sender; the sender leaving sends STOP).
   - **B1.16.16: installed 2026-11-22 19:28–19:32, the first test PASS** (19:35–19:37, Spotify; §2.4,
     §2.5). Both start-up lines were there (the load line at 19:34:20).
     - By eye and ear (the owner): "All looks good. Paused and resumed Spotify and once switched to
       music remote worked on music. Spotify let go so paused state music didn't restart Spotify even
       when switching back to NowPlaying."
     - The logs confirm: pause and resume taken and sent to Spotify; the target stayed `spotify` across
       the release; no `audio interruption` on the resume; the Music app started by hand cleared the
       target (`other audio is playing`, logged right after the interruption, about 8 s after the app
       switch while the owner started it); the three presses after that passed, also with NowPlaying in
       front again. No false clear, no `send failed`, no daemon restart, no reboot.
     - Only the owner's report, not in any log: that NowPlaying kept the cover and showed "Paused via
       Spotify", and what was heard.
     - The resume ran wake on play (`wake+launch`, the lock plugin enabled again): §4 question 6.
     - **Still untested:** next and previous while paused (what cspot does); the end of the Spotify
       session while paused (`remote-target` → `none`); AirPlay as the paused target (pause from the
       sender, the sender leaving); the keys with no source of ours active since audiod's start; the
       AirPlay echo fix of B1.16.14 (item 4); the iPad (no tweak there; audiod B1.16.16 and NowPlaying
       1.4.3 not installed).
   - Press about a second apart: the dock thins out rapid repeats (§2.5).
4. **B1.16.14, AirPlay** (phone, installed): the echo test of §3.1.
5. **B1.16.9** (iPad): NowPlaying 1.4.1, stages 1 → 2 → 3 via `remote-control.stage`.
   - Does `remote-control.log` fill from the dock keys, and with which subtypes?
   - Who gets the keys when the Music app was the last to play?
6. Still never reported: iPad rotation and cover sharpness (sharp covers need cspot 47d4760 as well).

The owner said on 2026-11-22 that installs may go ahead even while something is playing (§5.2). That
word was about installs; it did not mention the SpringBoard restart of §2.5, which still waits for the
owner's word on a playing device.

### 3.3 After those results
- **DockRemote swallow:** B1.16.15 is installed and took previous and next on its first test (§2.5).
  Finish the rest of §3.2's item 3; if it passes, decide with the owner whether swallow stays a flag or
  becomes the default under `dock_remote=on`. Every code seen so far is in the table (§10.5). Up and
  select presses that the dock does not send, and the repeats it thins out (§2.5), are not the tweak's to
  fix.
- **After B1.16.16's device test:** its first test passed on the phone (2026-11-22, §3.2): pause, resume
  and the hand-back to the Music app. Once next/previous while paused and AirPlay pass as well, the
  paused-source rule (§5.2) is done for the phone; the iPad has no tweak, but NowPlaying 1.4.3 keeps a paused source on screen there too once audiod B1.16.16
  is installed (check `voltest iap` first, §2.4).
- **A skip can count as a new start** (§2.5, seen at 18:42): when a track change is silent for longer
  than `release_ms` (1500 ms by default), audiod releases the source and takes it again about 2 s later,
  and that takeover runs wake on play again (NowPlaying launched, the lock plugin enabled again). Open:
  decide whether a skip should wake the screen and relaunch the app (§4). Nothing is changed yet. It is
  not specific to the remote: any skip with a long enough gap does it. Since B1.16.16 the source stays
  the remote target across such a gap, so a takeover `none -> X` with X still the target could be
  treated as a resume (no wake) in `onStartFromNone` (`audiod/README.md`, B1.16.16).
- **iPad dock remote:**
  - If a stage works, make it the default and drop the others.
  - If none works, the now-playing role needs real non-mixable playback, which would interrupt audiod's own
    non-mixable session. That is a design talk with the owner, not a quick fix.

### 3.4 Backlog: the owner decides (nothing built)
- **Bonjour keep-warm on the iPad:** cold discovery was slow once (AirPlay ~1 min; Spotify didn't list the
  unclaimed iPad), possibly a memory-starved mDNSResponder. Idea: cspot and raop periodically verify and
  re-announce `_spotify-connect._tcp` (:7864) and `_raop._tcp` (:5000).
- **A hung (not crashed) SpringBoard** keeps its pid, so B1.16.11 can't see it. A short-timeout SBS call
  could detect it. Very low priority.
- **The iPad shows an "Application Not Installed" accessory prompt when docked** (only the iPad lacks the
  Philips app). Suppressing it needs the bundle id the dock declares and a stub app. Nothing built.
- **The status bar's play icon** shows only when the built-in iPod app plays, not for the project's
  sources (the owner's observation, 2026-11-22). Idea: find out whether a daemon can raise it on iPhone
  OS 3. Nothing built, nothing investigated.
- Old device reads, never reported: B1.13 AirPlay volume and `dmcp.device-volume`; B1.12 Spotify pausing
  on an AirPlay takeover; the B1.5 `--no-backpressure` A/B.

### 3.5 B1.17: the rename `fidelio` → `dock-receiver`, with packaging (done in the tree; the devices in §3.0)
- Released by the owner on 2026-11-22 (§5.2); it had been on HOLD until the owner said device testing
  was done. Built the same day: `8ab46f4` (the rename), `7d1ced6`, `307be01`, `1f7d18d` (the notices,
  the packages, their suites) and the docs commit after them (§2.1).
- The plan and what was delivered against it, deviations included: [`docs/RENAME-PLAN.md`](RENAME-PLAN.md)
  §7. Every name: [`RENAME.md`](../RENAME.md).
- Left after the round: the devices (§3.0); the GitHub repository's rename (§4); the cleanup of the old
  files on the devices (`dock-receiver-setup cleanup --yes`, after the owner is satisfied; it leaves the
  `.pre-*` backups, `com.fidelio.syslogd` and iapd's logging keys alone, `packaging/README.md`); the old
  WSL clone `/home/user/fidelio` and the old build trees (§6.1), kept for now.

### 3.6 B2: lifted for packaging only (packaging done)
- The owner decided on 2026-11-18 that packaging (Debian packages with the launchd jobs and the install
  scripts) is built in the rename round (§3.5). **Done in B1.17** (`307be01`, `1f7d18d`;
  `packaging/README.md`, §6.7); not yet installed on a device (§3.0).
- Hardware switch-test automation stays on HOLD until the owner says go.

### 3.7 After the rename: release CI (next, after the devices)
- GitHub CI that makes a release on every merge to `main`, with an automatic version bump (the owner,
  2026-11-18). Nothing built.
- It needs one version number for the whole suite, stamped into the app and the daemons at build time.
  Since B1.17 there is one: `packaging/VERSION` (`1.5.0-1`, NowPlaying's version and a packaging
  revision; `tools/build-debs.sh` checks that its first part equals the app's
  `CFBundleShortVersionString`). The daemons do not carry it yet.
- **What the package builder gives it:** `tools/build-debs.sh` turns a verified `$BUILD_ROOT/arm-out`
  into the two `.deb` files and `SHA256SUMS`, reproducibly (fixed mtimes from
  `PACKAGE_SOURCE_DATE_EPOCH`, a second build compared), with only `tar`, `gzip`, `sha256sum`, `md5sum`,
  `find`, `sed` and `git`; and the suite `pkg_debs` checks the result. A workflow is then:
  `tools/build-arm.sh`, `tools/build-debs.sh`, `tools/run-host-tests.sh` (in that order, §6.4), and
  the release assets from `$BUILD_ROOT/debs`.
- The owner's direction (2026-11-21): a release includes the packages and the toolchain tarball.
  Publishing the packages waits for the owner's word; nothing was published in B1.17.
- **The toolchain tarball is published** since 2026-11-22 (the owner's word, §5.2): the release
  `toolchain-r3` of this repository (§6.1). No package has been published.
- Apple's SDK is no longer needed (§6.3), so the whole build can run on GitHub's runners (the owner
  chose this way on 2026-11-22, §5.2). A runner downloads the toolchain from that release and checks
  its sha256.
- A later toolchain revision gets its own tag and release (`toolchain-r4` ...): pack at the commit the
  tag will point at, verify as in §6.1, then publish.

---

## 4. Questions waiting for the owner
1. **Bonjour keep-warm** (§3.4): build it or not.
2. *(Answered 2026-11-22: B1.17 was released by the owner and built the same day; §5.2. The number is
   kept so that references to the later questions stay valid.)*
3. **Licence points found while preparing the release** (2026-11-21 and 11-22; facts gathered, not
   judged). Two are settled by the owner's decisions of 11-22 (§5.2); the rest are open. The
   open licence judgements stay with the owner; they did not block the rename (§5.2).
   - **Settled: the notice for the start-up code.** Apple's open-source licence asks, for object code,
     for "a prominent notice, in the code itself as well as in related documentation" saying where the
     source is. Since toolchain revision 3 the notice is in the code (§6.3);
     `toolchain/THIRD-PARTY-NOTICES.md` §9 is the documentation notice. The packages carry it since
     B1.17 (`packaging/THIRD-PARTY-NOTICES.md`, "The start-up code notice", in `/usr/share/doc/`).
     Whether a text section counts as "prominent" has not been judged.
   - **Settled: the terms of `cspot-ios3`.** The tree we build the Spotify library from has no licence
     file, while its original upstream states GPL-3.0-or-later. The top-level `LICENSE` now says that
     this binary as a whole is distributed under GPL-3.0-or-later. What its package must then carry is
     in `docs/RENAME-PLAN.md` §6.
   - Open: `bell`, the library cspot is built on and which is linked into `cspot-ios3`, has no licence
     file, neither in the tree we build from nor in its upstream repository (checked on 2026-11-22; the
     upstream was not re-checked in B1.17).
   - Open: the GPL lets a program be combined with the system's own libraries. `cspot-ios3` also holds
     the start-up code that we now build ourselves from Apple's sources under Apple's licence. Whether
     that exception covers it has not been assessed.
   - Addressed with the publication (2026-11-22): four toolchain components ask for source to be
     offered (the linker tools, a disassembler inside `otool`, ldid, libplist). The release that holds
     the tarball also holds source archives of cctools-port and ldid at the commits that were built and
     the Ubuntu source package of libplist, and the notices file and the release notes say so (§6.1).
     The session added these archives on its own judgement; the owner can remove them. Still not
     assessed: ldid is AGPL with a statically linked Apache-licensed libcrypto (upstream's combination);
     OpenSSL's source is named by address only.
   - **Drawn up in B1.17: the notices for the third-party code linked into the device binaries**
     (`packaging/THIRD-PARTY-NOTICES.md`, `7d1ced6`, read from the link maps; shipped in both packages
     under `/usr/share/doc/`). Facts it records for the owner's judgement, not judgements:
     - cspot and bell have no licence file in the tree that is built (bell's upstream not re-checked);
     - civetweb and picohttpparser have their licence text in file headers only (MIT; picohttpparser
       "MIT or the Perl license"), collected into notice files;
     - the ALAC decoder in `dock-raop`: its version is not recorded, and its headers say Apache-2.0 while
       the licence file beside them holds the APSL 2.0 text;
     - the AirPort Express key compiled into `dock-raop` has no licence statement;
     - the origin of bell's `utilities/aes.c` (linked into `cspot-ios3`) is not established; it has no
       copyright or licence line;
     - compiler-rt is not linked (only the project's own `ios3compat.o` from `libios3compat.a`).
4. **Release CI** (§3.7). Settled on 2026-11-22: the build needs no SDK any more (§6.3), so nothing of
   Apple's has to reach GitHub, and no runner on the owner's PC is needed. Open:
   - Settled on 2026-11-22: the runner downloads the toolchain tarball from the release `toolchain-r3`
     (§6.1). Building the toolchain from source in a workflow stays untried: the C++ runtime's build
     reads more headers than the sysroot's allow-list holds today.
   - On a runner: Ubuntu 24.04 with the packages of §6.1. The first build fetches the pinned upstreams
     and Apple's published releases from GitHub.
5. **The project's own framework headers: a licence judgement for the owner** (2026-11-22; facts
   gathered, not judged). `toolchain/sysroot/frameworks/` declares the part of Apple's UIKit, Foundation,
   CoreGraphics, QuartzCore, AudioToolbox, CoreAudio and AVFoundation interfaces that the sources use.
   The text and its arrangement are the project's own, and the work log of that stage records no
   look-up in an SDK header. Three things necessarily mirror Apple's interface:
   - the names and types of the classes, methods, functions and constants in use, because the device's
     libraries export exactly those; parameter names follow Apple's documentation;
   - seven protocols declared in full (NSObject, UIApplicationDelegate, UIScrollViewDelegate,
     UITableViewDataSource, UITableViewDelegate, UIAlertViewDelegate, UITextFieldDelegate): the compiler
     writes the whole method list of an adopted protocol into the binary, and the lists were read from
     the project's own objects;
   - three instance sizes (UIApplication, UIWindow, UIViewController), declared as reserved storage.

   A mechanical comparison with the SDK's headers on 2026-11-22 (counts only) shows where the text
   coincides: single declaration lines throughout, as the first point says, and three longer runs of
   identical lines, each a list that the interface fixes: 18 lines (the NSObject protocol's methods),
   10 (the fields of the audio stream description) and 9 (the three geometry structures). Elsewhere
   the longest run is 4 lines.

   The headers under `usr/include` and CoreFoundation's are Apple's published open-source files,
   fetched at build time and not kept in the repo, plus eight small patches of the project
   (`toolchain/sysroot/README.md`).
6. **A skip that wakes the device** (2026-11-22, §3.3): a track change silent for longer than
   `release_ms` counts as a new start, so wake on play launches NowPlaying and enables the lock plugin
   again. Should a skip wake the screen and relaunch the app, or should audiod treat such a gap as the
   same playback? Not yet put to the owner.
   - **A resume does the same** (phone, B1.16.16, 2026-11-22 19:35:40, §2.5): the paused source was
     released after 1.5 s of quiet, so the remote's resume ended in `takeover: 'none' -> 'spotify'`, which
     logged `wake: 'none' -> 'spotify' (wake+launch com.fidelio.nowplaying)` and `lock-plugin enabled
     Fidelio`; each pause before it had logged `lock-plugin disabled Fidelio`. NowPlaying was in front
     already, so the launch changed nothing on the screen. The question therefore covers a resume as
     well as a skip: with the source still the remote target (B1.16.16), such a takeover could count as
     a resume (no wake) (§3.3).
7. **DockRemote swallow as the default** under `dock_remote=on`, or a flag as now (§3.3): once the rest of
   the swallow test is done.
8. **Rename the GitHub repository** `azarzycki/fidelio` → `dock-receiver`, as `docs/RENAME-PLAN.md` §1
   says (asked on 2026-11-22, not yet answered). Until then the tree keeps the old repository URLs
   (the release `toolchain-r3` included); after it, they are updated in one commit.
9. **The AirPlay model string `am=Fidelio1,1`**: kept in B1.17 as part of the advertisement, with the
   advertised device names. Keep it, or change it?
10. **The Spotify credentials arrangement** (B1.17, a deviation from the plan's "root-only file"):
    `/var/mobile/dockreceiver/spotify-credentials.conf`, owner `mobile`, mode 600, read by the wrapper
    `cspot-run`. The job runs as `mobile`, and `cspot-ios3` takes the client id and secret only as
    command-line options, so they are visible in the device's process list while it runs, as before.
    Accept this, or add a cspot option that reads them from a file (a cspot change)?
11. **`dpkg --purge dock-receiver` deletes the data directory** `/var/mobile/dockreceiver` (settings,
    credentials file, logs). Keep that, or leave the directory on purge?
12. **The old device names and the bitrate carried into `settings.conf`**: the migration copies
    `cspot.name`, `raop.name` and `cspot.bitrate` from the old launchers' command lines (the new jobs
    have no options, so a value there would otherwise be lost). Keep that, or start from the defaults?

Publishing the packages as a release also waits for the owner's word (§3.7); nothing was published in
B1.17.

The volume default is no longer a question: the owner decided on 2026-11-18 (§3.1, §5.2).

---

## 5. Standing rules and decisions

### 5.1 Rules (verbatim intent; never break)
1. **Never** put the Spotify client-id/secret or ANY credential in any file. "Scripts are ok, no secrets."
2. **Never write the devices' root password** in any file, commit, log excerpt or message.
3. **B2 on HOLD** until the owner says go. Lifted for packaging only (2026-11-18; built in B1.17, §3.6);
   installing on a device still needs the owner's go-ahead (§8).
4. **VERIFY** every finding (code review, device report, a peer's diagnosis) **against the code** before
   changing anything. Peers have been wrong:
   - the "false negative" was right, but "unify on the hook" was wrong;
   - "call AVSystemController" was already done;
   - "loadView hard-codes 768x1024" wasn't true.
5. **Never rewrite git history** (no force-push, no amend of pushed commits).
6. **Keep `docs/HANDOFF.md` current:** update it in the same commit as each round (state, the rounds
   table, hashes, open work).
7. **Privacy:** the owner's email is for attribution only. Never send it, the owner's name or project
   details to external services.
8. **No PR** unless asked. **No model identifier** in commits, code or docs; the attribution trailer of
   rule 9 is the exception the harness prescribes.
9. **Commit trailers:** use the lines the current session's system reminder gives. Commits carry the
   owner's git identity from the PC.
10. **`main` moves by fast-forward only;** add a marker branch `audiod-b116NN` per device round
    (`audiod-b117` for B1.17).

### 5.2 Decisions log
- **11-15**, the queue: B1.16.4 first, then B1.18, then **B1.17 last**. "Wait for device results before B1.17."
- **11-15**, lock plugin precedence ("go with your suggestion"): the plugin is enabled only when audiod does
  not unlock the device for that start.
  - When no unlock is possible, it is enabled before the wake.
  - It is never enabled and then immediately unlocked past.
- **11-15**, entitlements: one audiod build with **4 keys** (device-proven). The A/B variant is gone.
- **11-15**, DockRemote: phone = `GSSendEvent` hook (B1.16.8). iPad = NowPlaying as a media controller
  (B1.16.9, greenlit). **No MobileSubstrate on the iPad.** Relayed by the device session; "unify on the hook"
  was relayed first, then corrected by the iPad's imports.
- **11-15**, from the owner directly: "keep the plugin as a backstop. It should be configurable from the
  NowPlaying app config [for] which mode. For me, I prefer to keep it launching on the phone." This became
  B1.16.10's `wake_mode`, with default `launch`.
- **11-15**: the cloud session accepted the device session's messages directly (the owner asked). Moot
  since 11-18.
- **11-15**, volume (relayed): a System/Software toggle per source, default System. Superseded by the
  11-18 requirement below.
- **11-18**, from the owner:
  - The cloud build session is retired; one session on the owner's PC does everything. "Now that we build
    here, the build session is deprecated. Assume all its responsibilities."
  - The private store is no longer needed. What is worth keeping moves into the repo.
  - Builds and test runs go in subagents.
  - Volume: absolute volume on this dock unless it is absolutely not possible (§3.1).
  - Packaging: two Debian packages, built in the rename round (§3.5, §3.6). The split follows the OS
    version, not the device type: a main package for every supported OS, and the dock-remote tweak for
    iPhone OS 3.x.
  - Spotify credentials: a root-only file on the device, never in a package or the repo, with a README
    that says how to fill it. (B1.17 made it a `mobile`-owned 0600 file instead, and says why; the
    choice is back with the owner, §4 question 10.)
  - After the rename: release CI on every merge to `main`, with an automatic version bump (§3.7).
  - Apple: avoid any exposure. Never put the SDK, or a link to a copy of it, in the repo, a release, a CI
    artifact, a cache or a log. `spotify/BUILD.md` carried a download link until 2026-11-18; it is removed
    from the current tree and remains in the history.
- **11-21**, from the owner:
  - A release includes the toolchain and the packages (after a research report on how comparable
    projects fared and an audit of what the builds contain). Nothing is published yet.
  - The start-up objects come from Apple's open-source Csu release, not from the SDK (§6.1).
  - The project's own code, the toolchain's files included, is MIT (the top-level `LICENSE`).
- **11-22**, from the owner:
  - The notice that Apple's open-source licence asks for goes into the code as well ("add the notice to
    our source"). That is toolchain revision 3 (§6.1, §6.3). It changes the sha256 of every binary.
  - `cspot-ios3` as a whole is distributed under GPL-3.0-or-later, because it links the Spotify library.
    The top-level `LICENSE` says so (`d1b20a9`). The repo's own source files that go into it stay MIT,
    and the other binaries are not affected.
  - The owner deleted the copy of the SDK tarball that the old link in the history pointed to. The link
    is dead (checked on 2026-11-22).
  - The Spotify receiver stays in the main package: "the structural distinction is enough". It is a
    program of its own beside the others, and no package of its own is made for it
    (`docs/RENAME-PLAN.md` §6).
  - "Deploy to the dock": the go-ahead for the queued installs. Done on the iPad that day; the phone was
    not reachable (§2.4).
  - The build without Apple's SDK: "#3 make our own, do that if it'll be functionally equivalent"
    (the third of three ways to get the ARM build onto GitHub's runners). Done the same day under one
    test: the switch is made only if every binary stays byte-identical. It did (§6.3).
  - Iterative build, compare and fix work is delegated to subagents on smaller models; the session
    plans, reviews the diffs and pushes (§6.1).
  - "Let's publish the tool chain": the toolchain tarball is published as the release `toolchain-r3`
    (§6.1). This word covers the toolchain only; packages and anything else still wait for the owner.
  - "Update the iPhone and iPad and let's do some device tests": the go-ahead for the phone's install
    and for the tests of §3.1 and §3.2, with the owner at the dock (§2.4, §2.5).
  - **Working rule:** all work, device work and log analysis included, is delegated to background
    subagents. The session plans, briefs, reviews and talks with the owner, and ends its turns at once,
    so that the owner's messages get through (they had been queuing behind long turns) (§6.1).
  - Installs may go ahead even while something is playing (§3.2).
  - "yes turn on the takeover for spotify/airplay" (18:41:17 phone time): `remote-tweak.swallow` was set
    on the phone (§2.4, §3.2).
  - AirPlay volume: the owner knows of "Software gain" for AirPlay alone as an option; nothing was
    decided (§3.1).
  - **A paused source keeps the remote and the screen** (after the 18:51 test, §2.5): "can we detect the
    music app playing and basically preserve our last state in nowplaying while paused, so if i pause
    spotify through the dock, the cover art stays up until spotify disconnects, airplay or music take
    over. release the play pause dock button takeover only if (not ours) music activity is detected
    while we're paused". The rule: a paused source of ours stays the remote's target until (a) it
    disconnects, (b) another source of ours takes over (that one is then the target), or (c) audio that
    is not ours plays while we are paused; no time limit; after (c) the keys stay with the Music app
    until one of our sources plays again. NowPlaying keeps the paused source's cover, title and artist
    until then. The lock-screen plugin is not part of it (no keep-lit while paused). B1.16.16 (§10.5,
    §10.6). B1.16.16 also clears the target when the source sends STOP (raop: the AirPlay sender left;
    cspot: the playlist ended), since nothing is left to resume.
  - **B1.17 released** (2026-11-22; it had been on HOLD "until the owner says device testing is
    done", §4 question 2). The owner: "Are we settled on the clean room work? Can we kick off rename ?"
    The session answered that the SDK-free work (§6.3) is technically settled, that the open licence
    judgements (§4 question 3, question 5) stay with the owner and do not block the rename, and started
    the round. Built that day (§2.1, §3.5); not yet on a device.
  - **Decided by the session in B1.17, and told to the owner:** the reference clone in WSL is
    `/home/user/dock-receiver` (the old `/home/user/fidelio` and the old build trees are kept for now,
    §6.1); the Windows clone's folder `C:\code\fidelio\github\fidelio` is deliberately not renamed (the
    session's own notes are keyed to it). Also the session's, within the plan, recorded in
    `docs/RENAME-PLAN.md` §7: the credentials file owned by `mobile`, the job plists copied by the
    postinst, the old device names and bitrate carried into `settings.conf`, `--purge` deleting the
    data directory, `am=Fidelio1,1` kept (§4 questions 9–12).
- **Naming** (from before): the app is "NowPlaying"; new components get neutral names; the advertised device
  names stay. The full rename is B1.17 only (done in the tree, 2026-11-22).

### 5.3 Naming rule
Describe the software by function. Use "Fidelio" only as Philips' dock name, with the
not-affiliated-with-Philips line in the README, and in the device names the receivers advertise ("iPhone
Fidelio", "iPad Fidelio", "Fidelio (AirPlay)") and the AirPlay model `am=Fidelio1,1` (§4 question 9).
**The rename is done in the tree** (B1.17, `8ab46f4`; `RENAME.md`): new code uses `dock-*`,
`com.dockreceiver.*`, `dockreceiver`, `NP*` and `DOCKRECEIVER_DIR`, never "fidelio". The devices keep
the old names until the packages migrate them (§3.0). Git history and
`docs/history/STATUS-2026-11-18.md` keep the old names; they are not rewritten.

---

## 6. Build conventions

### 6.1 Environment (the owner's Windows PC, WSL2)
- **Distro:** WSL2 `Ubuntu2404` (Ubuntu 24.04). It runs as root.
- **Reference paths.** cspot's and DockRemote's hashes depend on them. They are real directories, because
  `tools/_common.sh` resolves paths with `pwd -P`.

  | What | Path |
  |---|---|
  | toolchain | `/home/user/opt/armv6-ios3-toolchain` |
  | Apple's SDK (optional since `b5e3106`, §6.3) | `/home/user/sdk/iPhoneOS3.1.2.sdk` |
  | repo | `/home/user/dock-receiver` (since B1.17; before it `/home/user/fidelio`) |
  | `BUILD_ROOT` (left unset) | `/home/user/build` |

- **The WSL clone** `/home/user/dock-receiver` is a git clone of the Windows clone, so its `origin` is
  `/mnt/c/code/fidelio/github/fidelio`. Fetch and fast-forward on the Windows side first, then `git pull`
  in WSL.
  - **The reference path changed in B1.17** (the session's decision, the owner told): `cspot-ios3`'s
    hash depends on it, so the hashes of §2.3.1 hold at `/home/user/dock-receiver` only. The old clone
    `/home/user/fidelio` is kept for now (it reproduces the hashes of §2.3.2); it is no longer the
    reference and is not updated.
  - The Windows clone's folder `C:\code\fidelio\github\fidelio` is deliberately **not** renamed: the
    session's own notes are keyed to it. Likewise `C:\code\fidelio\cloud-build\` and the other folders
    under `C:\code\fidelio\`.
  - Build trees kept beside `/home/user/build`: `/home/user/build.b11616-fidelio` (the last build under
    the old names, B1.16.16 at `59d03a1`), `/home/user/build.b117-run1` (the first of the two
    from-nothing builds of `8ab46f4`) and `/home/user/build.b117-dirty` (a build of the uncommitted
    rename, before `8ab46f4`). The last two have the same `SHA256SUMS` as today's `/home/user/build`
    (§2.3.1). Remove them when the owner no longer needs them.
- **Build only inside the distro's own filesystem,** never under `/mnt/c`.
- **Builds and test runs go in subagents, one at a time** (the owner's wish; the scripts share one
  `BUILD_ROOT`). Since 2026-11-22 the iterative build, compare and fix work goes to subagents on
  smaller models, and the session plans, reviews the diffs and pushes (also the owner's wish).
  - Since the evening of 2026-11-22 **all work** is delegated to background subagents, device work and
    log analysis included. The session plans, briefs, reviews and talks with the owner, and ends its
    turns at once so that the owner's messages get through (the owner's rule, §5.2).
- **Packages:**
  `apt-get install clang-18 llvm-18 cmake ninja-build protobuf-compiler python3-protobuf libavahi-client-dev g++ unifdef flex bison`.
  - `g++` gives the host suite its `cc` and `c++`.
  - `unifdef`, `flex` and `bison` are needed when `tools/build-arm.sh` makes the sysroot itself (§6.3).
    Ubuntu 24.04's flex 2.6.4 and bison 3.8.2 were tested.
  - Since B1.17 the host suite also uses `curl`, `make`, `ar` (binutils) and `dpkg-deb` (for GNU bash
    4.0 and the package suites, §6.4); the runner checks for them.
  - `tools/_common.sh` prints the list if something is missing.
  - The host clang is Ubuntu 24.04's `clang-18` (18.1.3).
- **The toolchain's own files are in the repo** (`toolchain/`, since `5a2283c`); its tarball and the SDK
  are not.
  - **Toolchain tarball, revision 3, as published** (2026-11-22):
    `armv6-ios3-toolchain-linux-x86_64.tar.xz`, 3,978,132 bytes, sha256
    `6d97bee67d4897937694c43f9006f47288d756a2e106c7059da12ed309d5b94c`. It is the pack of commit
    `b2a8ee3`, which the tag `toolchain-r3` points at, and it is the asset of the release
    <https://github.com/azarzycki/fidelio/releases/tag/toolchain-r3>. It is the one installed in WSL
    since that day. On the PC the release's files are in
    `C:\code\fidelio\cloud-build\release-toolchain-r3\`.
    - Checked before it went out: two packs gave the same bytes; `pack.sh`'s comparison with the SDK ran
      (none of the 1,105 packed files has the sha256 of any of the SDK's 2,228 files); the tree holds no
      address, host name, session id or path of the owner's PC or devices; `tools/build-arm.sh` with
      this tarball installed gave `result: OK` and the hashes of §2.3; the asset was downloaded again
      from GitHub, also without a login, and has the sha256 above.
    - The same release holds `SHA256SUMS` and source archives: `cctools-port-904de2a71d4d.tar.gz`,
      `ldid-af86971ae72e.tar.gz` (`git archive` of the pinned commits; the project's patches apply to
      them) and the three files of Ubuntu's source package `libplist` 2.3.0-1~exp2build2. GitHub
      replaced the `~` in two of those file names by a `.`; the release notes say so.
      `lib/libplist-2.0.so.4` in the tarball has the sha256 of the library in Ubuntu's binary package of
      that version.
    - The pack of `c46beee` (3,978,908 bytes, sha256
      `c635235bc75ba22882f0e1c80abd5b59808c76034fb20092791348e6d9252c7f`) was the installed one until
      then. It is kept as `/home/user/opt/armv6-ios3-toolchain.r3-c46beee` and on the PC as
      `C:\code\fidelio\cloud-build\armv6-ios3-toolchain-linux-x86_64-r3.tar.xz`. The two differ in
      four text files only (`LICENSE`, `README.txt`, `THIRD-PARTY-NOTICES.md`, `BUILD.md`).
    - `toolchain/pack.sh --base <an unpacked toolchain>` rebuilds a tarball, byte for byte, from the repo
      at the same commit and the upstream-built parts of a base toolchain (`toolchain/README.md`,
      `spotify/BUILD.md` §6). Revision 3 was first packed with revision 2 as its base.
    - **Its sha256 belongs to the commit it was packed at.** The tarball holds copies of repo files: the
      top-level `LICENSE`, the build notes, the toolchain's own sources. `LICENSE` changed afterwards
      (`d1b20a9`), so a pack at a later commit differs in that one file and has another sha256. Its
      start-up objects, and every binary built with it, are the same. For a release, pack at the release
      commit and record that tarball's size and sha256 (§3.7).
    - Its start-up objects (`armv6-ios3/lib/startfiles/`) are built from Apple's open-source Csu, tag
      Csu-79, and each carries the source-availability notice of `toolchain/startfiles/notice.s` in a
      section `__TEXT,__csu_notice`. The linker copies it into every binary.
    - Against revision 2 it differs in the three start-up objects, in `notice.s` (new, next to their
      build script) and in five text files (`README.txt`, `VERSION`, `THIRD-PARTY-NOTICES.md`,
      `BUILD.md`, `build-startfiles.sh`). The host tools, the libc++ headers and the four ARMv6
      archives are revision 1's, byte for byte.
    - Its wrapper puts the toolchain's start-up objects ahead of the SDK's on every link (unchanged
      since revision 2).
    - It contains no file of the SDK (checked by hash against all 2,228 SDK files) and no link to one.
    - Binaries linked with it run on the iPad (iOS 5.1.1) and on the phone (iPhone OS 3.1.3) since
      2026-11-22 (§2.5).
  - **Revision 2** (2026-11-21; 3,976,540 bytes, sha256 `1f1cd630…b078`) is kept beside it as
    `…-r2.tar.xz` and as `/home/user/opt/armv6-ios3-toolchain.r2`. It is superseded: its start-up
    objects have no notice, and `tools/build-arm.sh` refuses it. Binaries linked with it were
    byte-identical to revision 1's.
  - **Revision 1** (3,923,700 bytes, sha256 `c07f80c4…dbff`) is kept beside it and as
    `/home/user/opt/armv6-ios3-toolchain.r1`. Do not publish it: it has no licence texts and an old copy
    of the build notes. `tools/build-arm.sh` refuses it.
  - The build trees of the earlier revisions are kept in WSL as `/home/user/build.r1` and `.r2`; the
    first of the two from-nothing builds with revision 3 is `/home/user/build.r3a`.
  - **SDK tarball** `iPhoneOS3.1.2.sdk.tar.xz`: 97,995,312 bytes, sha256
    `d5122c3bd2ddd217de918ff3635c590261424dc7f3a5e63d4cbd8417bec2d5e2` (verified identical to Apple's DMG).
    It is kept at `C:\code\fidelio\`. It is Apple's: never commit it, and never publish it or a link to a
    copy (§5.2). `spotify/BUILD.md` §1 says how it was checked. Since `b5e3106` the build does not
    need it; it is kept to confirm additions to the sysroot (§6.3).
  - Unpacking, if the distro has to be set up again:
    ```sh
    mkdir -p /home/user/opt && tar xf armv6-ios3-toolchain-linux-x86_64.tar.xz -C /home/user/opt
    # No SDK is needed to build. To build against Apple's SDK (§6.3), extract it so that
    # /home/user/sdk/iPhoneOS3.1.2.sdk/usr/lib/libSystem.B.dylib exists and set IOS3_SDK to that directory.
    ```
- **Running WSL from the PC session:**
  - Pipe a single-quoted heredoc into `wsl -d Ubuntu2404 -u root -- bash -s`. Multi-line commands passed
    through PowerShell lose quotes and backslashes.
  - Never use `nohup` or `&` inside WSL: the distro is torn down when the calling `wsl.exe` exits.
    Background the tool call instead.
  - Set a clean `PATH`, because WSL appends the Windows `PATH`:
    ```sh
    export PATH=/home/user/opt/armv6-ios3-toolchain/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
    ```
- **Baseline proved there on 2026-11-17:**
  - At `5dc865f`: `tools/build-arm.sh` exit 0, `result: OK`, in 79 s from nothing including the fetches.
    `tools/run-host-tests.sh`: `TOTAL host: 340 ok, 0 FAIL`, 22 of 22 suites on the first run, exit 0,
    575 s, host compiler g++ 13.3.0.
  - At `6721da9`: `tools/build-arm.sh` `result: OK` with all eight binaries equal to the published ones (47 s
    with cspot rebuilt, 14 s warm).
  - At `b8d8976`: `result: OK` twice (15 s, 13 s). Only voltest changed; it is the same on both runs.
- **Baseline with toolchain revision 3, 2026-11-22:**
  - `tools/build-arm.sh` from nothing, twice at the same path (91 s and 87 s): `result: OK` and the same
    `SHA256SUMS` both times. Warm at `c46beee`: 16 s, the same hashes (§2.3).
  - `tools/run-host-tests.sh`: `TOTAL host: 340 ok, 0 FAIL`, 22 of 22 suites on the first run, 580 s.
- **Baseline without Apple's SDK, 2026-11-22** (`b5e3106`; the SDK directory was hidden from every run):
  - `tools/build-arm.sh` from nothing, twice at the same path (100 s and 102 s, every fetch included):
    `result: OK` and the nine hashes of §2.3 both times; all 405 object files are identical between
    the two runs and to the build against the SDK. Warm: 14 s, with no network.
  - `tools/run-host-tests.sh`: `TOTAL host: 340 ok, 0 FAIL`, 22 of 22 suites on the first run, 576 s.
  - Trees kept in WSL: `/home/user/build.r3-sdk` is the last tree built against Apple's SDK,
    `/home/user/build.own-a` the first of the two built without it, and `/home/user/build` was the second
    (it has since been rebuilt by later rounds).
- **Baseline of B1.17, 2026-11-22** (at `/home/user/dock-receiver`):
  - At `8ab46f4`: `tools/build-arm.sh` twice from nothing, `result: OK`, the same hashes both times.
    Host suite: 529 ok, 0 FAIL, 28 suites (521 + the 8 new `np_source` checks). The first host run was
    stopped from outside at 16 of 28 suites, all passed up to then, because the PC ran low on memory
    (not a test failure; §11); the rerun, alone, was clean.
  - At `1f7d18d`: `tools/build-arm.sh` `result: OK`, every binary's hash unchanged from `8ab46f4`
    (§2.3.1); `tools/build-debs.sh` built both packages twice with the same sha256;
    `tools/run-host-tests.sh`: `TOTAL host: 849 ok, 0 FAIL`, 30 of 30 suites on the first run, 659 s
    (3 s build + 656 s tests).
- **BUILD.md** (`spotify/BUILD.md`) documents the toolchain in full: the tarball contents, the wrapper
  flags, the float ABI, `libios3compat`, the patches, and what was verified.
- **cspot's nanopb generator** needs a python3 whose protobuf still has `reflection.MakeClass`.
  `tools/_common.sh` (`ensure_nanopb_python`) builds a shim, or set `NANOPB_PYTHON`.
- **Pinned upstreams** (`tools/_common.sh`):
  - cspot = `sle118/squeezelite-esp32` @ `ee1720c7556ffec5f43329aadd47c20546bdbb79`
    (`components/spotify/cspot`), plus `spotify/patches/cspot-squeezelite-ee1720c-0001-armv6-ios3.patch`
    applied with `git am`.
  - mbedTLS `v3.6.7` (`068ff080b3…`).
- **`BUILD_ROOT`** defaults to `build` **next to** the repo (`/home/user/build`) and must never be inside
  it. Under it: `deps/`, `host/`, `arm/`, `arm-out/`, `logs/` and `host-test-logs/`.

### 6.2 Compiling for iOS 3.1 (the rules that bit)
- **Wrappers:** `armv6-ios3-clang` / `clang++` / `strip` / `ldid` / `check` / `lipo` (plus
  `arm-apple-darwin9-*`).
  - The target is `armv6-apple-ios3.0`, softfp with VFPv2, ARM mode.
  - Leave `IOS3_MIN_VERSION` at 3.0. A higher value makes ld64 emit compressed dyld info.
- **Objective-C:** `-fno-objc-arc -fobjc-exceptions`.
  - No ARC, no libarclite, **no blocks**: use `sortUsingFunction:context:`, not `sortUsingComparator:`.
  - No `UIDeviceFamily` or `UI_USER_INTERFACE_IDIOM` in the SDK. Set the plist key by hand; detect a
    tablet at run time (shorter screen side > 600 pt).
  - `UIGraphicsBeginImageContext` only.
- **Compile ObjC with `-x objective-c -c`, then link in a separate command.** The wrapper appends
  `libios3compat.a` at link time, and `-x objective-c` would apply to it too.
- **C++:** `-std=c++17 -stdlib=libc++` (static libc++ 18) for the daemons; the SDK's libstdc++ 4.2.1 is the
  default.
  - **The toolchain's libc++ and `libios3compat.a` are armv6 only**, so any armv7 slice can't link them.
  - None is needed: our armv6 binaries run on the iPad's A4.
- **iOS 4+ APIs** (remote control and so on) are called through `respondsToSelector` / `dlsym`. Their
  constants are re-declared locally (see `NPRemoteControl.m`).
- **Private APIs:** `dlopen` + `dlsym` / `objc_getClass` / `sel_registerName`, never linked.
  - Check `method_getTypeEncoding` / `method_copyArgumentType` before calling a guessed signature (the
    keep-lit fallback and voltest do this).
  - Inside SpringBoard or iapd, `@try`-guard everything: a crash there is a respring, or a dead dock until
    a reseat.
- **Signing:** strip, then `armv6-ios3-ldid -S<plist>` (no space after `-S`; `-e` dumps the entitlements),
  then `armv6-ios3-check`.
  - audiod and lockctl are signed in CMake POST_BUILD with `common/dock-springboard.entitlements.plist` (was
    `common/fidelio-springboard.entitlements.plist`)
    (4 keys: launchapplications, activateawayviewplugins, opensensitiveurl, openurlswhenlocked).
  - `LINK_DEPENDS` relinks them when the plist changes.
- **Lock plugin:** linked `-bundle` with `-Wl,-U` for the two `SBAwayViewPluginController` class symbols.
  `armv6-ios3-check` reports exactly those 2 errors, shown as "FAIL(2 by-design)"; nothing else is
  acceptable.
- **DockRemote:** links with no `-undefined dynamic_lookup`, and `armv6-ios3-check` is fatal. MSHook* and
  `GSSendEvent` are resolved at run time.
- **The ISA baseline:** every binary is ARMv6 with **0 post-ARMv6 instructions**, checked by
  `tools/isa_scan.py`.
  - It decodes as the ARM1176 and as ARMv7, skips literal pools, and exits 2 if it cannot scan.
  - A grep of an armv6 disassembly is blind (`movw`/`udiv` print as `<unknown>`).
- `#include "audiod/proto.h"` (repo root on the include path), not `"proto.h"`.
- **`knownType()`** in `common/AudiodFraming.cpp` is a whitelist. **Every new message type must be added
  there**, or both ends silently drop it.

### 6.3 tools/build-arm.sh (the ARM build and verification)
The toolchain comes from `PATH` or `IOS3_TOOLCHAIN`. **No SDK is needed** (since `b5e3106`, 2026-11-22):
- **The sysroot.** The wrapper compiles and links against the directory in `IOS3_SDK`.
  - `IOS3_SDK` unset: `tools/build-arm.sh` makes the project's own sysroot in `$BUILD_ROOT/sysroot/root`
    with `toolchain/sysroot/build-sysroot.sh` and uses it. This is the normal build.
  - `IOS3_SDK` set: that directory is used as it is, Apple's `iPhoneOS3.1.2.sdk` or a sysroot made
    earlier by `build-sysroot.sh`. Apple's SDK is needed for one thing only: to confirm an addition to
    the sysroot against the real libraries and headers.
  - `report.txt` names the sysroot in its second line. The project's own is identified by the first 16
    hex digits of the sha256 of `$BUILD_ROOT/sysroot/CONTENTS`; on 2026-11-22: `8ee9290184708740`.
  - `arm/sysroot` under `BUILD_ROOT` is something else: the install prefix of the ARMv6 mbedTLS.
- **What the project's own sysroot is** (`toolchain/sysroot/README.md` has the detail):
  - Link libraries: 11 stub libraries generated from `stubs/manifest.tsv`, the list of the 426 symbols
    that the binaries and the toolchain's archives import, with each library's install name and versions.
  - `usr/include` and CoreFoundation: 264 headers. 240 are files of Apple's published open-source
    releases (xnu, Libc, CF and others, pinned by commit and sha256 in `headers/sources.tsv`), as
    published or with the release's own install processing; 10 carry one of eight small patches of
    the project; 13 are generated by Apple's `mig`, which the script builds from source; 1 is our own.
  - The other frameworks: nine headers of our own (`frameworks/`), declaring only what the sources use.
  - The releases are fetched once into `$BUILD_ROOT/deps/apple-oss` and checked against their pins on
    every use. A warm build reads nothing from the network.
- **Each of the three parts is an allow-list.** A symbol, a header or a declaration that is not there
  was not needed so far. How to add one is in `toolchain/sysroot/README.md`. The rule: confirm it
  against Apple's SDK (`IOS3_SDK=…`) or on a device first.
- **Proof of equivalence** (2026-11-22): against the project's own sysroot, with the SDK directory hidden
  from the build, all 409 compiles give byte-identical objects and the nine binaries have the hashes of
  §2.3. Each of the four stages was held to that test.
- **What a future source could notice** is listed in `toolchain/sysroot/README.md` ("What differs and
  does not matter today"). The main points:
  - there is no `usr/include/stdint.h`: clang's own is used, so `__WORDSIZE` is undefined;
  - the ARM machine headers come from a 2017 release;
  - the framework headers declare only today's usage.
- **Objective-C:** the four component build scripts compile with `-Werror=objc-method-access`. With
  allow-list headers a message to an undeclared method would otherwise compile, with only a warning,
  to wrong code.
- **A local tool, not in the repo:** `/root/sdkfree/harness/replay.sh --sysroot DIR` in WSL replays all
  409 compiles against a sysroot and reports the objects that differ (about 50 s). It was the test
  bench of this work and is the quick check for a change to the sysroot.

About the script itself:
- **It builds:** mbedTLS; audiod and lockctl (CMake); raop; cspot (deps); NowPlaying, both lock bundles,
  DockRemote and voltest (their `build.sh`).
- It collects everything into `$BUILD_ROOT/arm-out`, then verifies each binary: size, sha256,
  `armv6-ios3-check`, the post-v6 count, and the entitlement keys against the plist.
- **Since `c46beee` it needs toolchain revision 3** (`TOOLCHAIN_MIN_REVISION` in `tools/_common.sh`;
  from `5a2283c` it needed revision 2). It stops before it builds anything if the toolchain is older or
  has no `VERSION` file. It also checks, for every binary:
  - its link map (`$BUILD_ROOT/arm/maps/<step>/`): no `.o` file or archive member under `$IOS3_SDK` was
    linked, and exactly one start-up object, the toolchain's;
  - `tools/startcheck.py`: the start-up code inside the binary. It exists because start-up objects
    built with the Csu Makefile's `ld -r` step are wrong and still pass `armv6-ios3-check` and
    `isa_scan.py`;
  - the notice (also `tools/startcheck.py`): the section `__TEXT,__csu_notice` must hold, byte for byte,
    the text for the file type. An executable needs the text of `crt1.o` ("This program contains
    start-up code built from Apple's open-source Csu release…"); a dylib or a bundle needs the text of
    `dylib1.o` and `bundle1.o` ("This library contains glue code…"). `report.txt` shows it in the
    `notice` column as `program` or `library`. `strings -a FILE | grep Csu-79` prints the text;
  - the toolchain's three start-up objects against the sha256 pins in `tools/_common.sh`.

  All of them were shown to fail on deliberately bad set-ups (the commit messages of `5a2283c` and
  `c46beee`).
- **The notice survives the project's own steps:** `-dead_strip`, `armv6-ios3-strip -x` and the signing
  leave it in place (tested with an executable, a dylib and a bundle).
- **A size trap:** the notice fits into the padding of `__TEXT` in all eight binaries today. A binary
  whose `__TEXT` is within about 460 bytes of a 16 KB boundary would grow by 16 KB.
- It writes `arm-out/report.txt`, `SHA256SUMS` and `verify/*`.
- **Exit codes:** 0 OK, 1 verification failed, 3 setup or build error. About 15 s warm.
- **Reproducible:** three clean rebuilds give identical hashes.
  - cspot embeds absolute paths, so its hash only matches at the same paths.
  - DockRemote.dylib embeds its `-o` path as its install name, so its hash depends on the output directory
    string. build-arm.sh always uses `$BUILD_ROOT/arm/dockremote`.
- **The build date pin** (since `6721da9`): cspot-ios3 also embeds a build date.
  - civetweb's build identifier is `__DATE__` (`bell/external/civetweb/civetweb.c`; `BUILD_DATE` is not
    defined). It is the only `__DATE__`/`__TIME__` use in the cspot tree and in the project's sources.
  - A fresh build dir on another day gave the same size but another hash. 57 bytes differed: the date
    string, LC_UUID and two signature page hashes.
  - `tools/build-arm.sh` now builds cspot with `SOURCE_DATE_EPOCH` = `CSPOT_SOURCE_DATE_EPOCH`
    (`tools/_common.sh`; 1794744000 = 2026-11-15 12:00 UTC, the date inside the existing cspot-ios3).
  - The value is part of the `arm/cspot` stamp.
- **Each round, rebuild once from the clean commit** for the report (`repo: … @ <sha> (git status clean)`).

### 6.4 tools/run-host-tests.sh (the host suites)
It builds the host targets (`$BUILD_ROOT/host/...`) and runs **30 suites one after another**. They share
`/tmp/audiod-*` dirs and ports: never run two at once, and **don't run CPU-heavy work alongside**, since
the suites are timing-sensitive.

**Run `tools/build-arm.sh` first** (since B1.17): `pkg_debs` builds the packages from
`$BUILD_ROOT/arm-out` and fails without it.

| suite | checks | suite | checks | suite | checks |
|---|---|---|---|---|---|
| framing | 26 | silence | 22 | lockplugin | 8 |
| aqsink | 28 | crossfade | 5 | preempt | 11 |
| aqsink_bare | 23 | producers | 21 | ap_preempt | 11 |
| writeframe | 2 | pacing | 8 | ap_volume | 11 |
| settings | 23 | wake | 11 | volume | 11 |
| settings_dmn | 29 | unlock | 23 | layout | 30 |
| control | 8 | arbiter | 13 | lock_layout | 10 |
| cover_url | 6 | dockvol | 25 | keylog | 17 |
| remote_keys | 65 | remote_core | 22 | remote_tgt | 37 |
| np_source | 23 | pkg_setup | 316 | pkg_debs | 83 |

- **Baseline: 928 ok, 0 FAIL, 30 suites** (B1.17 after the review, `c8b1475`; 849 at `1f7d18d`; 529 at `8ab46f4`, when `np_source` grew 15 → 23;
  521 before B1.17 added `pkg_setup` and `pkg_debs` and extended `np_source`; 447 before B1.16.16 added `remote_core`, `remote_tgt` and `np_source`; 434 before B1.16.15 extended `remote_keys`; 423 before B1.16.14 extended `dockvol`; 354 before B1.16.13 added `keylog` and `remote_keys`; 340 before B1.16.12
  added `dockvol`).
- **It needs `cc` and `c++`** (apt `g++`): the plain tests call them by name, and they are CMake's default
  compilers. Since `6721da9` the script checks for both.
- **Exit codes:** 0 all passed first time; 1 a persistent failure; **2 passed only after a re-run**
  (`producers` has a known timing flake in its HWVOLUME check, so report exit 2 honestly); 3 setup error.
- **A second rare `producers` failure:** in the first run of B1.16.12 the check "sender volume -144 dB
  plays at unity through audiod" failed once. It did not reproduce: the full suite on e590ef1 was rerun
  (354 ok, 0 FAIL, 23 of 23 suites on the first run), and `producers` alone passed 8 of 8 on e590ef1 and
  8 of 8 on 1a6ce9e. Treated as a rare timing flake, cause unknown.
- **Environment:**
  - `RUNS` (default 3); `SUITES="producers pacing"` runs only those; `SKIP_BUILD=1`.
  - `SUITE_TIMEOUT` (900 s); `PYTHON`; `STRICT_COUNTS=1` (an ok count off the baseline fails the suite).
  - `HOST_CC` / `HOST_CXX` apply to a fresh configure only.
- **Logs:** `$BUILD_ROOT/host-test-logs/<timestamp>/` (`latest` links to it), with `summary.txt`. A failed
  run's `/tmp/audiod-*` dirs are copied there.
- About **11 minutes** (659 s on the owner's PC at B1.17; 575 s before it): run it in the background,
  alone, and wait for it to finish. Do not run it while other heavy work runs on the PC: on 2026-11-22
  a run was stopped from outside at 16 of 28 suites for low memory on the PC (§11).
- **The package suites (B1.17):**
  - `pkg_setup` (`packaging/hosttest/test_setup.sh`, 243 checks) runs the install scripts in fake
    device roots under **GNU bash 4.0**, the devices' `/bin/sh` (they have 4.0.17). The runner builds it
    once from the release tarball, pinned by sha256 (`BASH40_URL`, `BASH40_SHA256` in
    `tools/_common.sh`; `ensure_bash40`, into `$BUILD_ROOT/host/bash-4.0`), with a `PATH` of only the
    tools both devices have plus stand-ins for `launchctl`, `killall`, `uicache`, `su`, `dpkg` and `df`.
  - `pkg_debs` (`packaging/hosttest/test_debs.sh`, 77 checks) builds the packages from
    `$BUILD_ROOT/arm-out` and checks format, owners, modes, paths, the binaries byte for byte, control
    fields, no secret or private address, reproducibility, and `install.sh` against stand-ins for ssh
    and scp.
  - The runner needs `curl make tar gzip dpkg-deb ar md5sum` for them.
- **Adding a suite:** add a row to `SUITE_TABLE` (name, baseline count, command), update the baseline
  comment, and if it is a plain C test, add its build line to `build_host` and to the existence check.

### 6.5 Host-test conventions
- The tests drive the real host build of audiod with `--pcm-out` and `--event-log` (lines
  `<ms> <kind> <from> <to> <reason>`), plus `--status-dir` when settings.conf is involved (re-read once a
  second).
- **Mock waker** (`audiod/src/DisplayWake.h`): it logs `mock waker: …`.
  - `AUDIOD_MOCK_LOCKED=1`.
  - `AUDIOD_MOCK_UNLOCK=ok|stuck|refuse|missing`: `stuck` = accepted, but launches would still answer 3,
    as on the real iPad.
  - `AUDIOD_MOCK_SB_RESTART_MS`: SpringBoard's pid changes from 100 to 200.
- **Mock hardware volume:** `AUDIOD_MOCK_HW_VOLUME`, `AUDIOD_MOCK_HW_CHANGE_FILE`. Test hook:
  `AUDIOD_STALL_ACTIVE_MS`. Since B1.16.14 a new value in the change file is seen by `get()` at once and
  reported by `takeChange()` at the next poll, as on the device; `AUDIOD_MOCK_HW_POLL_MS` sets the poll
  interval (default 100 ms).
- **Mock other audio** (B1.16.16, `audiod/src/OtherAudio.*`): `AUDIOD_MOCK_OTHER_AUDIO_FILE` names a file;
  `1` as its first character means "audio that is not ours is playing".
- **Mock dock** (B1.16.12, `audiod/src/HardwareVolume.*`): `AUDIOD_MOCK_IAP=ok|unsupported|fail|query-fail`
  (unset = IAP unavailable); each call it "makes" is logged as `mock iap: IAPAudioSetVolume(L) -> R`.
  Test hook: `AUDIOD_DOCK_FORWARD_MIN_MS` (the coalescing window, default 200 ms).
- **Practice:**
  - A new test must be shown to **fail against the previous binary**. Build the old commit in a temporary
    `git worktree`, as B1.16.6 did: 6 FAILs against b051bfc. B1.16.12's `dockvol`: 11 FAILs against
    1a6ce9e (the other 3 are the "never told" checks, which the old binary passes by doing nothing).
    B1.16.14's new checks: 6 FAILs against the code before the fix (its first commit, which only changes
    the mock).
  - Plain-C layout cores have host tests that pin the device-proven frames: `nowplaying/src/NPLayout.c` with
    `layout_test.c`, and `nowplaying/lockplugin/LPLayout.c` with `lock_layout_test.c`.
  - B1.16.13: device code that can be plain C or C++ is kept apart from the Objective-C so the host can
    run it: `nowplaying/src/NPKeyLog.c` with `keylog_test.c`, and the tweak's `DockRemoteKeys.cpp` with
    `tweak/DockRemote/hosttest/keys_test.cpp` (records built as the phone sent them).

### 6.6 Per-component notes
- **audiod:** CMake (`audiod/CMakeLists.txt`), host and ARM, via the scripts.
  - `dock-audiod --help` lists every flag.
  - The registry (§10.2) is bound to the real options: `read()`/`write()`, `selfCheck()` logs drift.
- **NowPlaying:** `nowplaying/src/build.sh` (the file list is inside). `gen_images.py` makes the icons,
  tab glyphs, placeholders and launch images, and the build fails if one is missing.
  - The version is in `NPStatus.h` (`NP_APP_VERSION`) and `Info.plist` (1.5.0 since B1.17), and
    `packaging/VERSION` must start with it (§6.7).
  - After installing on a device, run `su mobile -c uicache` (device family, icons, URL scheme). On
    2026-11-22 the update from 1.4.1 to 1.4.2 on the phone was a swap of the bundle directory without
    `uicache` and without a SpringBoard restart, and the new app ran at once (§2.4).
- **Lock plugin:** `nowplaying/lockplugin/build.sh OUTDIR lockbundle|bundle`. The binary and Info.plist are
  the same for both.
- **DockRemote:** `tweak/DockRemote/build.sh` (C++17/libc++ static, ObjC++). The decode and the press logic
  are in `DockRemoteKeys.cpp` (no Objective-C, host-tested).
- **voltest:** `audiod/probe/build.sh`.

### 6.7 tools/build-debs.sh (the packages, since B1.17)
- `tools/build-debs.sh [--arm-out DIR] [--out DIR] [--no-repro-check]`, after `tools/build-arm.sh`:
  writes `dock-receiver_<version>_iphoneos-arm.deb`, `dock-receiver-dockremote_<version>_iphoneos-arm.deb`
  and `SHA256SUMS` to `$BUILD_ROOT/debs`. The layout and the install scripts are in
  `packaging/README.md`; the maintainer scripts and `dock-receiver-setup` are one script,
  `packaging/dock-receiver/setup.sh`.
- **Version:** `packaging/VERSION` (`1.5.0-1`): NowPlaying's version and a packaging revision. The script
  refuses a first part that is not the app's `CFBundleShortVersionString`.
- **Format:** what the devices' dpkg 1.18 reads: an ar archive of `debian-binary` (`2.0`),
  `control.tar.gz` and `data.tar.gz`, gzip only (no xz on the devices). Assembled by the script, not by
  `dpkg-deb`, so every byte is fixed: mtimes `PACKAGE_SOURCE_DATE_EPOCH` (`tools/_common.sh`,
  1795348800 = 2026-11-22 12:00 UTC), sorted members, owners 0:0, explicit modes, `gzip -9n`. It builds a
  second time and compares the sha256.
- **Input checks:** it packages only an `arm-out` that passed `build-arm.sh`'s verification
  (`verify/.bad` = 0), and each binary must match its line in `arm-out/SHA256SUMS`.
- **Exit codes:** 0 built and reproducible; 1 the second build differs; 3 setup error.
- **Installing from the PC:** `packaging/install.sh [--check] [--debs DIR] root@<device>` over OpenSSH
  (it adds the legacy `ssh-rsa` options the devices' OpenSSH 6.7 needs, and `scp -O`); §8.

---

## 7. Round checklist

1. **Verify** the reported problem in the code (§5.1.4). Write the fix plus host tests; show the new test
   fails against the old binary when practical.
2. **ARM build** (`tools/build-arm.sh`): check warnings (`grep -i warning` in `$BUILD_ROOT/logs/arm-*.log`;
   only the toolchain's `-mllvm -fast-isel=0` notes are expected).
3. **The packages** (`tools/build-debs.sh`, §6.7): both `.deb` files and `SHA256SUMS` in
   `$BUILD_ROOT/debs`, reproducible. If the app's version changed, change `packaging/VERSION` with it.
   Stage them for the devices in `C:/code/fidelio/cloud-build/deploy-<round>/`.
4. **Full host suite** in the background, after the ARM build (§6.4; `pkg_debs` needs `arm-out`).
5. **Commit.** Message: a subject `B1.N…: …`, a body saying what was verified and why, then the trailers
   (§5.1.9).
   - **Never claim test results that haven't finished.** If the commit comes before the suite ends, word
     the message accordingly.
6. **Rebuild ARM from the clean commit** (pull it into the WSL clone first, §6.1), then fast-forward `main`
   and add the marker in one push from the Windows clone, whose `origin` is GitHub:
   ```sh
   git fetch -q origin main && git merge-base --is-ancestor origin/main <sha> && git push origin <sha>:refs/heads/main <sha>:refs/heads/audiod-b116NN
   ```
   (`audiod-b117` for B1.17.) A tools-only round gets no marker (voltest and `6721da9` have none).
7. **`docs/HANDOFF.md`:** make sure it is current. §5.1.6 puts the update in the round's own commit: the
   state (§2), the rounds table, the hashes (binaries and packages), the open work (§3).
8. **Install**, on the owner's go-ahead (§8). From B1.17 on, by package: `packaging/install.sh --check`,
   then `packaging/install.sh`, from the staged `.deb` files (§8). The respring and, on the phone, the
   undocked iapd restart for the tweak stay manual steps.
   - Until a device is migrated, the old hand install of §8 applies to it (from `/home/user/build/arm-out`;
     the app: `su mobile -c uicache` afterwards; the lock bundles need a respring; DockRemote is installed
     undocked, then `killall iapd`, on the phone with `redock.sh`). Do not mix: a hand-copied binary
     under a new name does not replace the old one the old jobs start.
9. **Tell the owner the outcome in plain words.**

---

## 8. Device work
- The session has SSH to both devices, as root, from the PC.
- It runs device tests **only on the owner's go-ahead**. Hands-on steps wait until the owner is at the
  dock.
- **Logs only, trimmed. Never credentials** (§5.1.1, §5.1.2).
- **Installs are by package since B1.17** (`packaging/README.md`; not yet done on either device, §3.0):
  - From the PC: `packaging/install.sh --debs <dir> --check root@<device>` (the dry run; changes
    nothing), then `packaging/install.sh --debs <dir> root@<device>` (copies, dry run, asks, `dpkg -i`;
    on iPhone OS 3 also `dock-receiver-dockremote`). It uses OpenSSH with the legacy options the
    devices need; ssh asks for the password, which is never written anywhere (§5.1.2).
  - On the device, `dock-receiver-setup` (`/usr/sbin`): `check` (dry run), `migrate` (the postinst
    again), `rollback` (remove both packages; the old setup comes back), `cleanup [--yes]` (removes
    `/var/mobile/fidelio` and `/var/dockreceiver-migration`; after it no rollback), `respring` (refuses
    while a source plays; `--even-if-playing` overrides).
  - The install never restarts SpringBoard or iapd by itself. The respring waits for the owner's word
    and nothing playing (§2.5); the tweak loads at the next undocked `killall iapd` (or a reboot).
  - The install puts every file in place under a new name and renames it, and a failure before its point
    of no return undoes the run and leaves the old setup running.
- **The hand install below is the old way, and still what both devices have today** (§2.4). It applies
  to a device until the packages have migrated it.
- **cycript only standalone,** never `-p SpringBoard` (§2.5).
- **Never restart iapd while the phone is docked.** It half-negotiates the accessory, and only reseating
  the phone fixes it.
  - On the phone, `/var/mobile/fidelio/b1/redock.sh` (2026-11-22) does it safely with the owner at the
    dock: it waits for the owner to lift the phone off, restarts iapd only when the route is neither
    `USB` nor `LineOut`, and waits for the re-dock. Both DockRemote loads of that evening went this way.
    (It is a hand-made helper on the phone, not part of the packages; `cleanup --yes` removes it with
    `/var/mobile/fidelio`.)
- **Replace a file that has run by rename, never by writing new content into it** (§2.5, measured on the
  phone 2026-11-22). Copying a new audiod over the installed path (`cp`, the same inode) got it killed at
  every launch (launchd `Exited: Killed`, no line from the binary). The procedure that works: stage the
  file, `cp stage/X X.new`, give it the old file's owner and mode, check its sha256, then `mv -f X.new
  X`. A dylib that a running process has mapped (DockRemote.dylib in iapd) goes the same way, with a
  temporary name that does not end in `.dylib` (MobileSubstrate would load it). Restoring a backup goes
  the same way (copy the backup to a new name, then `mv`). App bundles: move the old directory aside
  and unpack the new one.
- **Only one audiod may run at a time.**
- **A device observation is evidence, not truth:** verify it against the code (§5.1.4).
- **Probes and traces available:** `voltest`, the plugin trace (`lockplugin.debug` → `lockplugin.log`),
  `remote-events.log`, `remote-control.log`, and audiod's stdout and event log.
- Device probes found the 4th entitlement, the false negative, the dim behaviour, iapd's imports and the
  route with no volume control.

---

## 9. Files on the devices (the data directory, all user `mobile`-readable)
**Today, on both devices, the data directory is `/var/mobile/fidelio/`** (the old setup, §2.4). The
B1.17 code uses **`/var/mobile/dockreceiver/`** (`DOCKRECEIVER_DIR`, `common/DataDir.h`), with the same
file names; the packages create it and copy the settings and flag files over at the migration (§2.4,
`packaging/README.md`). The list below holds for either directory unless it says otherwise.
- **New with the packages** (in `/var/mobile/dockreceiver/` only):
  - `spotify-credentials.conf` (mobile, 600): `SPOTIFY_CLIENT_ID=` and `SPOTIFY_CLIENT_SECRET=`, read by
    `cspot-run`; filled by the migration from the old setup, never printed. Without values the Spotify
    job waits and reads the file again every 30 s. (The old setup keeps them in the iPad's `creds.env`
    and in the phone's launcher scripts: never print those, §11.)
  - `keepalive.conf` (optional): `KEEPALIVE_HOST=<address on the LAN>`.
  - `logs/{audiod,raop,cspot,keepalive}.log`: the jobs' output, moved to `<log>.1` above 1 MB.
  - `install.log` (root): what the install scripts did, never a credential.
- **Written by the daemons:**
  - `audiod.sock`; `active-source` (rewritten every 10 s; "none" when idle).
  - `remote-target` (B1.16.16): the dock remote's target, one line, `spotify`, `airplay` or `none`;
    replaced atomically at once on a change and every 10 s; `none` at audiod's start and clean stop.
    Read by DockRemote (B1.16.16) and NowPlaying 1.4.3 (§10.5, §10.6). A file older than 30 s counts as
    absent.
  - `status-spotify.plist` / `status-airplay.plist` (and `.json`); `cover-spotify.jpg` / `cover-airplay.jpg`.
  - `settings-<daemon>.manifest.plist`.
- **`settings.conf`:** `<daemon>.<key>=<value>` lines, written by the Settings tab, live-reloaded about
  once a second for live options (others need a restart).
- **`lockplugin.plist`:** `KeepScreenOn`, written by audiod (the single owner).
- **Lock plugin trace:** `lockplugin.debug` (the flag), `lockplugin.log`.
- **DockRemote:** `remote-tweak.observe`, `remote-tweak.swallow`, `remote-tweak.disabled` (the kill
  switch), and `remote-events.log` (the load line on every iapd start, plus the observe lines).
  - `remote-keys.log` (B1.16.13): one line per completed key press, `<unix seconds> <code> <name>
    <taken|passed>`, written by the tweak (as `mobile`, mode 0644) while `remote-tweak.observe` or
    `remote-tweak.swallow` is set; started afresh above 16 KB. NowPlaying 1.4.2's Technical tab shows the
    last six.
  - On the phone, as of the evening of 2026-11-22, `remote-tweak.observe` and `remote-tweak.swallow` are
    both set (§2.4).
- **`b1/`** (old setup only, `/var/mobile/fidelio/b1/`): the hand-installed binaries
  (`fidelio-audiod`, `fidelio-raop`, `fidelio-lockctl`, `cspot-ios3`), launcher scripts,
  the `.bak` backups and the stage directories; on the phone `redock.sh`, the undock, iapd restart,
  re-dock helper of §8.
- **NowPlaying 1.4.1 and later:** `remote-control.stage` (1–3), `remote-control.log`; and the old probe
  `remote-probe.on`, which logs to `remote-events.log` from the app.
- **Installed elsewhere, today (old setup):**
  - The lock bundles `Fidelio.bundle` (phone) and `Fidelio.lockbundle` (iPad) in
    `/System/Library/SpringBoardPlugins/`. Respring afterwards.
  - DockRemote: `/Library/MobileSubstrate/DynamicLibraries/DockRemote.{dylib,plist}` (filter: iapd).
    Replace it by rename (§8), then restart iapd undocked (`b1/redock.sh` on the phone).
  - The app: `/Applications/NowPlaying.app` (`com.fidelio.nowplaying`).
  - The owner's jobs `com.fidelio.audiod`, `.raop`, `.cspot`, `.keepalive` in `/Library/LaunchDaemons`;
    on the phone also `com.fidelio.syslogd` (a debugging aid the packages leave alone).
- **Installed elsewhere, after the migration (packages):** `/usr/libexec/dock-receiver/`,
  `/usr/bin/dock-lockctl`, `/usr/sbin/dock-receiver-setup`, the jobs `com.dockreceiver.*` in
  `/Library/LaunchDaemons` (copies of `/usr/share/dock-receiver/launchd/`), `/Applications/NowPlaying.app`
  (`com.dockreceiver.nowplaying`), `NowPlaying.bundle` and `NowPlaying.lockbundle` in
  `/System/Library/SpringBoardPlugins/`, DockRemote as before (phone), `/usr/share/doc/dock-receiver*/`,
  and `/var/dockreceiver-migration/` (the old setup, for rollback). `voltest` is not packaged.

---

## 10. Technical reference

### 10.1 audiod wire protocol (`audiod/proto.h`)
- **Messages:** HELLO 1, AUDIO 2, FLUSH 3, STOP 4, META 5, VOLUME 6, WELCOME 7, HWVOLUME 8, PREEMPTED 9,
  CONTROL 10.
- **HELLO:** `{u8 version (2 = reads server messages), u8 prio, u8 channels 2, u8 bits 16, u32 rate 44100,
  char name[32]}` behind an 8-byte header `{u32 type, u32 len}`.
- **CONTROL:** `{u8 verb}`. PLAY 1, PAUSE 2, TOGGLE 3, NEXT 4, PREV 5.
  - A client whose HELLO name is `"control"` is a controller, not a source.
  - Its verb goes to the active source; PLAY with nothing active resumes the last one.
  - B1.16.16: with nothing active and a remote target (a paused source of ours, §10.5), every verb goes to
    the target, TOGGLE and PLAY as PLAY. Without a target the line above holds.
  - `common/AudiodControl.*`, NowPlaying's `NPSendControl` and DockRemote's `forward()` all use one-shot
    connections.
- **VOLUME:** a 4-byte gain, or the 8-byte `AudiodVolume2` `{gain, raw slider, flags SLIDER|APPLY}` (only
  after WELCOME).
  - **WELCOME:** `{absolute?, level as slider}`.
  - **HWVOLUME:** sent when the hardware level changes.
- **Arbiter:** the most recent producing source wins.
  - A challenger steals only if its producing run started after the active source's.
  - STOP or disconnect releases at once; quiet longer than the release time releases.
  - Tunables (`takeover_ms`, `hold_ms`, `release_ms`, `producing_gap_ms`, `lead_ms`) are advanced settings.
  - The default crossfade on a switch is 30 ms (PcmRing).
  - Pacing: the SocketSink lead is 750 ms.

### 10.2 Settings registries (`common/Settings.*`; manifests drive the NowPlaying Settings tab)
Defaults are in brackets.
- **audiod** (`audiod/src/AudiodSettings.h`, bound to `ServerOptions` / `Tunables`):
  - **Volume:**
    - `spotify_volume` and `airplay_volume_mode`: absolute ("Absolute (drive the speaker)") or software
      ("Software gain") [absolute].
    - `max_volume` [100]; `hw_volume_curve` (linear|log|cubic) [linear].
  - **Source switching:** `crossfade_ms` [30], `preempt_pause` [on], `preempt_delay_ms` [500].
  - **Display:**
    - `wake_on_play` [on]; `wake_mode`: launch ("Open NowPlaying") or screen ("Screen only") [launch].
    - `unlock_on_play` [on], `lock_plugin` [off], `keep_screen_on` [off].
  - **Controls:** `dock_remote` [off].
  - **Advanced:** `takeover_ms` [400], `hold_ms` [1500], `release_ms` [1500], `producing_gap_ms` [1000],
    `lead_ms` [750] (restart to change).
- **raop** (`airplay/src/main.cpp`):
  - `name` ["Fidelio (AirPlay)"], `latency_ms` [1200].
  - `airplay_volume` (auto|honor|ignore) [auto].
  - `airplay_preempt` (pause-then-disconnect|pause-only|disconnect|none) [pause-then-disconnect].
  - `preempt_verify_ms` [3000], `preempt_cooldown_ms` [10000].
- **cspot** (`spotify/cli-src/main.cpp`):
  - `name` ["iPhone Fidelio"; the iPad advertises "iPad Fidelio"].
  - `bitrate` (96|160|320) [96], `initial_volume` [50], `volume_curve` (log|cubic|linear) [log].
  - `cover_size`: large ("640 px") or listed [large].
- **Command line:** an option given as a command-line flag is locked. The Settings tab shows it but can't
  edit it.

### 10.3 Wake, unlock, lock plugin (audiod `Server.cpp`, `DisplayWakeApple.cpp`)
- **Playback start from nothing** (not a switch, not a reconnect), debounced to one wake per 5 s:
  - **`wake_mode=launch`:** `SBDimScreen(port,false)`, `GSEventResetIdleTimer`, then
    `SBSLaunchApplicationWithIdentifier(com.dockreceiver.nowplaying)` (B1.16.x builds: `com.fidelio.nowplaying`).
    - On 3, with `unlock_on_play` on: `SBSOpenSensitiveURLAndUnlock(nowplaying:, true)` once.
    - true → `wake: device locked, unlock accepted (nowplaying:)`, and the plugin stays off for that start.
    - false → `unlock request failed`, and the plugin goes on.
  - **`wake_mode=screen`:** undim and reset the idle timer only, with the plugin enabled first:
    `wake (screen only)`.
- **The plugin (`lock_plugin`):** `SBEnableLockScreenBundle(port, 1/0, "NowPlaying")` (B1.16.x builds: `"Fidelio"`,
  which is what the devices log today: `lock-plugin enabled Fidelio`).
  - Disabled when nothing is active. B1.16.16 leaves this as it is: a paused remote target does not keep
    the plugin on (§5.2).
  - **B1.16.11 `syncLockPlugin`:** applies a live setting change, keeping the unlock rule; it disables
    under the enabled name. While enabled, it polls SpringBoard's pid every 2 s (sysctl `KERN_PROC_ALL`)
    and re-enables 4 s after a new pid.
- **The plugin itself** (`NowPlayingLockPluginController`; was `FidelioLockPluginController`):
  - `+rootViewController` returns a retained singleton.
  - The 1 Hz refresh runs only while visible: it reads `active-source` and `status-*.plist`, and does
    keep-lit (§2.5).
  - The layout comes from `LPLayout.c` (tablet pinned to B1.16.3; phone band y 116..384).
  - Trace lines: `layout:`, `keep-lit:`, `refresh: … keepLit=held|held:arg-float|held:arg-double|dimmed|off|unavailable`.

### 10.4 Volume (`HardwareVolumeApple.cpp`; §3.1)
- **API:** `AVSystemController` (Celestial, dlopen'ed): `setVolumeTo:forCategory:` / `getVolume:forCategory:`
  on "Audio/Video", and `AVSystemController_SystemVolumeDidChangeNotification` on the local center, with a
  1 s poll.
- **In absolute mode:** a slider-driven source plays at unity. An APPLY sets `curve(slider)`, clamped by
  `max_volume`; external changes are broadcast.
- **The dock (B1.16.12):** the system volume does not reach an accessory by itself (§2.5). After an APPLY
  has set the system volume, `Server::applySlider` also tells the dock through iapd.
  - `IAPAudioSetVolume(float)` and `IAPAudioIsVolumeControlSupported(BOOL*)` from
    `/System/Library/PrivateFrameworks/IAP.framework/IAP`, with `dlopen` and `dlsym` at start-up; the
    prototypes are voltest's. Missing framework or symbol: the forward is off. The start-up line
    `dock volume (B1.16.12): available (...)` or `unavailable (<why>)` says which.
  - Each forward first asks `IAPAudioIsVolumeControlSupported` and calls `IAPAudioSetVolume` with the
    level just set only when the answer is yes (1 docked on the phone, 0 off the dock).
  - Never for an external change (`pollHardwareVolume`): the dock's buttons set the phone's volume, and
    the dock already knows its level. The external path has no call to the dock at all, so there is no
    loop. `IAPAudioSetVolume` does not change the system volume (iapd's notifyApp=0) and the dock sends
    nothing back (§2.5), so it cannot produce a false `external` either. Were a dock to echo the level,
    it would at worst show once as `external` (iapd's steps of 1/255 are wider than audiod's 0.002 dead
    band) and stop there, since an external change is never forwarded.
  - Both calls are synchronous MIG round trips to iapd on audiod's run-loop thread. A drag is coalesced:
    at most one forward per 200 ms; a set inside that window is held, and the latest held level goes
    out from the run loop when the window ends. An external change drops a held level, and so does
    (B1.16.14) a hardware level read again at the flush that is no longer the one audiod set: the poll
    may not have seen a dock press yet. That logs `hwvolume: held dock level 0.450 dropped (1 held): the
    level changed outside (now 0.300, not yet reported)`, and the poll reports the change as usual.
  - **Echoes (B1.16.14).** A source may send a level audiod reported to it straight back as a volume
    command (an AirPlay sender does, §2.5). `Server::noteReport` keeps the last 8 levels reported to each
    client (in `sendWelcome` and `sendHwVolumeTo`, the only two places a level is reported) with their
    times. `applySlider` ignores a set within 3 s of such a report and within one dock step (1/255) of
    its level: no system-volume set, no dock call, no report. It logs one trace line, not an event:
    `hwvolume: 'airplay' echoed 0.600 (reported 813 ms ago): ignored`. A clamped request (above
    `max_volume`) is not checked, so the clamp and its report work as before. Accepted cost: a genuine
    move to within one dock step of a level reported in the last 3 s is ignored.
  - Logging: the event's reason ends `; dock=<result>`, one of `ok`, `failed` (the call returned 0),
    `deferred` (held), `unsupported`, `query-failed`, `unavailable`. Example:
    `hwvolume: 'spotify' -> '0.400' (set 0.400 by 'spotify' slider 26214; dock=ok)`. A held level that
    goes out later logs `hwvolume: dock told 0.300, the latest of 2 held sets: dock=ok`; one dropped
    logs `hwvolume: held dock level 0.550 dropped (1 held): the level changed outside`.
  - A set that failed is not forwarded; its line (`set FAILED ...`) is unchanged.
- **In software mode:** the producer applies the B1.7 curve.
- **Type encodings** (`voltest`):
  - `getActiveCategoryVolume:andName:` = `c16@0:4^f8^@12`
  - `setActiveCategoryVolumeTo:` = `c12@0:4f8`
  - `setVolumeTo:forCategory:` = `c16@0:4f8@12`
  - `routeForCategory:` and `volumeCategoryForAudioCategory:` = `@12@0:4@8`
- **Also in 3.1.2 Celestial:** `currentRouteHasVolumeControl`, `pickableRoutesForCategory:`,
  `changeActiveCategoryVolumeBy:`, `attributeForKey:` (…`CurrentlyActiveCategoryAttribute`,
  `AudioIsPlayingSomewhereAttribute`, `HeadphoneVolumeLimitAttribute`, …), and the route notifications.
- **audiod's AudioSession** (`AudioQueueSink`) is **non-mixable MediaPlayback**.

### 10.5 DockRemote (phone) and the iPad's remote
- **Phone, B1.16.13 and B1.16.15** (the hook point of B1.16.8, the decode measured on the phone, §2.5):
  - `MSHookFunction(GSSendEvent)`, resolved where it is exported.
  - A key is a **type 4001** `GSEventRecord`: header 52 bytes (type @0, subtype @4, flags @40, infoSize @48),
    info 8 bytes: the word @52 is 0, the word @56 is the command code.
  - It is decoded (`DockRemoteKeys.cpp`) only when type is 4001, infoSize is 8, the word @52 is 0 and
    the flags are exactly `0x400` (down) or `0x800` (up). Everything else passes untouched, type 101
    included.
  - Codes: play/pause 0, next 20 and 3, previous 21 and 4, menu 22, select 23, up 24, down 25.
    play/pause, menu, select, up and down come as `0x400` + `0x800`; next and previous as one `0x800`.
    All four forms are measured (§2.5): next came as 20 on the first press of a run and 3 on quick
    repeats; previous came as 21 once and as 4 on every press since. Code 3 was assumed by symmetry when
    B1.16.15 was built and seen on the phone the same evening.
    A two-event press of 3 or 4, should one come, is taken like play/pause: one verb, both events dropped.
  - Log names: `previous` for 21 and 4, `next` for 20 and 3 (the line's code tells the forms apart).
  - A press (an optional `0x400` and the `0x800` of the same code) is decided once, at its first event.
    With `remote-tweak.swallow` and DockRemoteCore's conditions (`dock_remote=on`, a source of ours is
    the remote target, audiod reachable), play/pause, next and previous (all their codes) are taken: every event of
    the press is dropped and the verb (TOGGLE, NEXT, PREV) goes to audiod once, at the first event. Other
    codes always pass. Conditions not met: the press passes as before.
  - **When a key line is written:** at the press's `0x800` event, once per press, while
    `remote-tweak.observe` or `remote-tweak.swallow` exists: `<unix seconds> <code> <name>
    <taken|passed>` in `remote-keys.log` (one open/write/close, 0644, truncated above 16 KB). With neither
    flag the hook writes nothing (a press already taken is still followed to its `0x800`).
  - The observe log line: `gs type= subtype= flags= infoSize= port= [KEY code= flags=] hdr=[…] info=[…]`,
    plus `TAKEN code= flags= -> audiod verb= (sent|send failed)` for a press taken over.
  - Proven by disassembling the 3.1.2 SDK's GraphicsServices (armv6): `GSEventAccessoryKeyStateChanged`
    builds that record and calls `GSSendSystemEvent`, which is `GSSendEvent(rec, the purple system port)`.
    iapd builds the record itself.
- **The remote target (B1.16.16, audiod `Server::updateRemoteTarget`):** the active source; with nothing
  active, the last active source, paused, until it disconnects from audiod, sends STOP, another source
  of ours becomes active (then that one), or audio that is not ours plays (`OtherAudioIsPlaying`, polled
  every 500 ms while paused). No time limit. audiod writes it to `remote-target` (§9) and sends every
  verb there while nothing is active (§10.1).
  - The tweak reads `remote-target` (one `stat` and one small read); without it, or with one older than
    30 s, it reads `active-source` as before. The socket check still gates every take, so a file left by
    a dead audiod takes nothing.
  - Device trace and rule: §2.5, §5.2. Load line `DockRemote B1.16.16: hooked GSSendEvent ...` (B1.17: `DockRemote B1.17: hooked ...`).
- **iPad, B1.16.9:**
  - NowPlaying's `NPRemoteControl.m`: the window class `NPWindow` can become first responder.
  - `NPApplication` (the principal class, `NPEventProbe.m`) catches remote-control events (type 2, subtypes
    100–109) in `sendEvent:` and at the end of the responder chain, and handles each once.
  - Stage 2 dlsyms `MRMediaRemoteSetCanBeNowPlayingApplication`. Stage 3 plays silence in a **mixable**
    session through `AVAudioPlayer`, because a non-mixable one would interrupt audiod.

### 10.6 NowPlaying
- **Structure:**
  - `NPAppDelegate` (was `FidelioAppDelegate`) sets `setIdleTimerDisabled:YES` at launch, so an unlocked device with the app in
    front never auto-locks.
  - The 1 Hz tick refreshes `NPStatus` (was `FidelioStatus`).
  - The cspot watchdog runs `killall -9` on a stale status.
- **Layout:** `NPLayout.c`; the phone is pinned to the B1.16.1 frames, and the tablet layout is size-driven
  with rotation.
- **Settings tab:** built from the manifests, with info (ⓘ) alerts. Changing a non-live option restarts the
  daemon.
- **A paused source stays on screen** (1.4.3, B1.16.16): the 1 Hz refresh reads `active-source`, then
  `remote-target` (both older than 30 s = absent); `NPSource.c` picks the source. Active: as before. None
  active but a target: that source's track, cover, title and artist stay, the line reads "Paused via ...",
  the buttons stay enabled. Target cleared: "Idle". Another source takes over: that one. No
  `remote-target` (an older audiod): as 1.4.2 (`nowplaying/APP.md`).
- **"Daemons not running"** (1.5.0, B1.17): when the data directory is missing, or nothing in it is
  fresh (no `active-source` within 30 s and no status file modified within 30 s), the Now Playing tab
  says so and which case it is, instead of an empty screen. The decision is plain C (`NPDaemonsState`,
  `NPIsDirectory` in `NPSource.c`), host suite `np_source` (23 checks).
- **Technical tab, "Dock remote"** (1.4.2): the last six lines of `remote-keys.log`, newest first, polled at
  2 Hz while the tab is visible (`NPKeyLog.c`; `nowplaying/APP.md`).

---

## 11. Pitfalls
- **Host suites are timing-sensitive:** `producers` flakes, and `pacing` takes 155 s. Don't run them
  alongside builds. Editing the repo during a run changes the report's "repo after the run" line.
- **`-x objective-c` on a link line** breaks the link (§6.2). The **toolchain's libc++ is armv6 only**.
- **SpringBoard and iapd crashes:** use cycript `-p SpringBoard` never; DockRemote must never block iapd's
  thread.
- **iPad:** no syslog (file traces only), and the shell tools are missing (§1). For a sha256 there:
  `openssl dgst -sha256 FILE`.
- **audiod's start-up summary shows the options before `settings.conf` is applied.** The lines
  `wake-on-play: …` and `unlock-on-play: …` are printed by the Server's constructor from the command
  line and the defaults; the file is read afterwards (`audiod/src/main.cpp`). The effective values are
  in `settings-audiod.manifest.plist` (`value`, `origin`). Seen on the iPad on 2026-11-22: the log said
  `unlock-on-play: on` while the file's `off` was in force.
- **Installing over SSH:** never print the launcher scripts, `creds.env` (old setup),
  `spotify-credentials.conf` (packaged setup) or a process list with arguments: they hold the Spotify
  credentials (`cspot-ios3` still takes them on its command line, §4 question 10). Check a process with `killall -0 NAME`. Replace an
  installed binary or dylib by rename, never with `cp` over it: on the phone the new content in the old
  inode is killed at every start (§8).
- **Phone:** no cycript, so ship a probe binary. No `ps` either.
- **The packages' scripts and the devices' tools** (B1.17, §2.5): `/bin/sh` is bash 4.0; use `sed -r`,
  never `sed -E`; no awk, plutil or defaults anywhere, and the iPad lacks most of coreutils. The host
  suite `pkg_setup` enforces this with GNU bash 4.0 and a restricted `PATH`, but only a device confirms
  the exact tool versions (§3.0).
- **The repo path is part of a hash** (§6.3): `cspot-ios3` built in `/home/user/fidelio` and in
  `/home/user/dock-receiver` from the same commit differ. Compare hashes only between builds at the same
  path; the reference since B1.17 is `/home/user/dock-receiver`.
- **Memory on the PC** (2026-11-22): a host-suite run was stopped from outside at 16 of 28 suites
  because the PC ran low on memory (nothing had failed). Run the suite alone, with no build, test run or
  other heavy work beside it, and rerun it whole if it is cut short (§6.4).
- **`voltest`'s first line has two `route=` fields:** Audio/Video's and MediaPlayback's. On the dock they
  normally read `USB` and `LineOut`. A script that reads the wrong one reports `LineOut` although nothing
  is wrong. This happened once (2026-11-22, 18:30): iapd had not restarted and the dock had not
  re-identified.
- **Testing dock keys:** the dock thins out rapid repeats and does not pass a hold on as a hold, and its up
  key arrives unreliably (§2.5). Press about a second apart and count the dock's packets in iapd's log,
  not the presses.
- **WSL** (§6.1):
  - Multi-line commands passed through PowerShell lose quotes and backslashes. Pipe a single-quoted heredoc
    into `wsl -d Ubuntu2404 -u root -- bash -s`.
  - Never `nohup` or `&` inside WSL: the distro is torn down when the calling `wsl.exe` exits. Background
    the tool call instead.
  - WSL appends the Windows `PATH`: set a clean one.
  - Build only inside the distro's own filesystem, never under `/mnt/c`.
  - One build or test run at a time: the scripts share one `BUILD_ROOT`.
- **The sysroot's allow-lists** (§6.3):
  - CMake's configure probes (`check_function_exists` and the like) link against the stub libraries. A
    function that is missing from `stubs/manifest.tsv` is reported as absent, without any error, and the
    build then takes another code path. After adding a dependency or a probe, read
    `CMakeConfigureLog.yaml` (`toolchain/sysroot/README.md` has the grep).
  - `armv6-ios3-check` against the project's own sysroot only confirms that an import is in the
    manifest. That iPhone OS 3.1 really has the symbol is confirmed when the row is added.
  - The stub tree must never contain `usr/lib/libstdc++.dylib`: clang would then link it by name.
  - The toolchain's wrapper still says "SDK" in its messages, and its default C++ runtime
    (`IOS3_CXX_RUNTIME` unset) needs the SDK's libstdc++ headers, which the project's own sysroot does
    not have. The build always uses libc++. Both are for the next toolchain revision.

---

## 12. Appendix

### 12.1 `main`, newest first (subjects shortened)
The first five lines are B1.17, on the branch `b117-rename` when this was written; `main` is
fast-forwarded to them right after the docs commit, with the marker `audiod-b117` (§2.1).
```
(docs)  docs: B1.17, the rename and the packages: the record under the new names   (marker audiod-b117)
1f7d18d B1.17 (part B 3/3) host suites pkg_setup and pkg_debs
307be01 B1.17 (part B 2/3) two Debian packages whose install scripts are the migration
7d1ced6 B1.17 (part B 1/3) third-party notices of the device binaries
8ab46f4 B1.17 (part A) the rename fidelio -> dock-receiver in the source tree, NowPlaying 1.5.0
51b22fe docs: B1.16.16 on the phone: install by rename, the paused-target test
59d03a1 B1.16.16 the dock remote keeps a paused source (remote target), NowPlaying 1.4.3   (marker audiod-b11616)
fb57a59 docs: the phone's evening of 2026-11-22: B1.16.12 to B1.16.15 installed, the key display, the first takeover test
86ce0b7 B1.16.15 DockRemote takes code 4 as previous and code 3 as next   (marker audiod-b11615)
23f407d B1.16.14 (2/2) audiod ignores a source's echo of a reported level   (marker audiod-b11614)
5f519ec B1.16.14 (1/2) mock hardware volume: get() sees an outside change at once
8af19cb B1.16.13 the build's hashes in the handoff; the stub manifest's importer fields   (marker audiod-b11613)
619c552 B1.16.13 the dock keys' measured decode, a key log, the presses in NowPlaying's Technical tab
e590ef1 B1.16.12 tell the dock a source's volume (IAPAudioSetVolume)   (marker audiod-b11612)
1a6ce9e docs: what the logs rule out for the phone's reboot after the respring
945ffc1 docs: toolchain published; the phone on the revision 3 builds; first packet log of the dock
b2a8ee3 Toolchain: name where revision 3 is published   (tag toolchain-r3)
5ef0218 docs: the build without Apple's SDK in the handoff; the licence note names the sysroot patches
b5e3106 tools: build against the project's own sysroot by default; IOS3_SDK optional
831ac15 toolchain: framework headers of our own, build-sysroot.sh (sysroot stage 3)
926ea54 toolchain: sysroot headers from Apple's open-source releases (sysroot stage 2)
55d1a82 toolchain: stub link libraries from a symbol manifest (sysroot stage 1)
d1b20a9 LICENSE: the Spotify receiver binary as a whole is GPL-3.0-or-later
c46beee toolchain: revision 3 (the source notice inside the start-up objects; every binary a new sha256)
5a2283c toolchain: revision 2 (own start-up objects, licence texts, pack script), sources in the repo, MIT
b8d8976 tools: voltest shows iapd's side of the volume, can call IAPAudioSetVolume
dfbbc3d docs: the project record moves into the repo
6721da9 tools: cspot build date pinned, host compiler check
5dc865f tools: voltest probe (B1.16.12 diagnosis)
7330752 B1.16.11 lock plugin live setting + respring
4158342 B1.16.10 wake_mode
8eec470 B1.16.9 NowPlaying media-controller prototype
4ed6a5a B1.16.8 DockRemote GSSendEvent hook (phone)
e3e448e B1.16.7 lock plugin phone layout + 3.1.3 keep-lit
aeed5b1 B1.16.6 4 keys, accepted unlock = result, keep-lit
b051bfc B1.16.5 DockRemote MSHookMessageEx lookup
47d4760 B1.18 universal NowPlaying, 640 px covers
ab0c7aa tools: host runner, ARM build, isa_scan
880ae13 B1.16.4 unlock on play, nowplaying: scheme
0b90521 B1.16.3 lock plugin draws (+rootViewController)
272c7e5 B1.16.2 DockRemote observe hook (wrong target)
32af243 B1.16.1 transport clip fix + Settings icon
9698db9 gitignore build-arm/
a5ba534 B1.16 settings layout, controls, control channel
8fb2fb8 naming: NowPlaying
4a346b8 B1.15 settings single source of truth + entitlements
86bc392 B1.14 settings manifest + config tab
6a96f4f B1.13 AirPlay absolute volume
ffa2494 B1.12 pause the losing source
807f92d B1.11 lock plugin + lockctl
5b81dcd B1.10 volume-steal/reconnect/autorelease fixes
8e2bc8c B1.9 absolute Spotify volume + wake on play
… earlier: Milestone A, B1, B1.1–B1.8 (base ce75a50)
```
