# HANDOFF — the dock-receiver stack (`azarzycki/fidelio`), state as of 2026-11-22

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

**Read in this order:**
1. §0, the resume checklist.
2. §2, the current state.
3. §3 and §4, the open work and the questions waiting for the owner.
4. §5, the rules and decisions.
5. §6 and §7, the build conventions and the round checklist.

§8–§12 are reference. Other documents:
- [`docs/RENAME-PLAN.md`](RENAME-PLAN.md): the plan for B1.17, the rename (on hold).
- [`docs/history/STATUS-2026-11-18.md`](history/STATUS-2026-11-18.md): the frozen per-round record with every
  earlier hash. Tasks 36 … 23 come first, newest on top; the older tasks follow.
- Component docs: [`spotify/BUILD.md`](../spotify/BUILD.md) (the toolchain),
  [`spotify/CSPOT.md`](../spotify/CSPOT.md), [`airplay/AIRPLAY.md`](../airplay/AIRPLAY.md),
  [`nowplaying/APP.md`](../nowplaying/APP.md), [`audiod/README.md`](../audiod/README.md),
  [`audiod/DESIGN.md`](../audiod/DESIGN.md), [`tweak/DockRemote/README.md`](../tweak/DockRemote/README.md).

---

## 0. Resume checklist (one session, on the owner's Windows PC)

1. **The repo:** the Windows clone is `C:\code\fidelio\github\fidelio`; its `origin` is GitHub
   (`azarzycki/fidelio`). Run `git fetch` first and compare with `origin/main` (§2.1).
2. **The build environment** is a WSL2 distro on the same PC (§6.1): toolchain, a second clone and
   `BUILD_ROOT` at the reference paths. Apple's SDK is no longer needed to build (§6.3).
   - Bring the WSL clone up to date: fetch and fast-forward on the Windows side first, then `git pull` in
     WSL.
   - Builds and test runs go in subagents, one at a time (§6.1).
3. **Prove the baseline:**
   - `tools/build-arm.sh` → `result: OK` with the hashes in §2.3.
   - `tools/run-host-tests.sh` → `TOTAL host: 354 ok, 0 FAIL`, 23 suites, about 10 minutes. Run it alone
     (§6.4).
4. Then work through §3 in order.

---

## 1. The project in one page

A headless audio receiver for two jailbroken devices docked in a **Philips Fidelio DS9000** speaker dock.
"Fidelio" is Philips' product name and is used only descriptively (§5.3).

| Device | Model | OS | CPU | Notes |
|---|---|---|---|---|
| the **phone** | iPhone 2G (iPhone1,1) | iPhone OS 3.1.3 | ARMv6 (ARM1176) | MobileSubstrate present (stub + libsubstrate); **no cycript**; has syslog |
| the **iPad** | iPad1,1 | iOS 5.1.1 | ARMv7 (A4); runs our armv6 binaries | **no MobileSubstrate** (deliberately; the install was stopped); **has cycript**; **no syslog** (use file traces); ~13 MB free RAM; lacks head/tail/tr/cut/sort/awk/nohup |

**Components** (one monorepo, everything built ARMv6 for iPhone OS 3.1; since 2026-11-22 against the
project's own sysroot, before that against Apple's iPhoneOS 3.1.2 SDK, with identical binaries; §6.3):
- **`fidelio-audiod`** (`audiod/`): the arbiter daemon.
  - It owns the AudioQueue output. Sources stream PCM to it over the Unix socket `/var/mobile/fidelio/audiod.sock`.
  - It picks the active source, applies volume, wakes and unlocks the display on play, enables the lock
    plugin, and routes transport controls.
  - It is signed with the 4 SpringBoard entitlements.
- **`cspot-ios3`** (`spotify/cli-src`, on a patched upstream cspot): the Spotify Connect receiver.
- **`fidelio-raop`** (`airplay/`): the AirPlay v1 (RAOP) receiver, with DACP for transport.
- **`NowPlaying.app`** (`nowplaying/src`): the dock display.
  - Tabs: Now Playing, Technical, and Settings (built from the daemons' manifests).
  - One universal binary (B1.18).
  - Bundle id `com.fidelio.nowplaying`; URL scheme `nowplaying:`.
- **`fidelio-lockctl`**: a CLI to enable/disable the lock plugin; entitled like audiod.
- **Lock-screen plugin** (`nowplaying/lockplugin`): `Fidelio.lockbundle` on the iPad, `Fidelio.bundle` on
  the phone; same binary. It shows the track on the lock screen and keeps it lit.
- **`DockRemote.dylib`** (`tweak/DockRemote`, phone only): a MobileSubstrate tweak in iapd that turns the
  dock's IR remote play/pause/next/prev into audiod controls.
- **`voltest`** (`audiod/probe`): a device probe of the system-volume categories (diagnosis tool).
- The daemons run on the devices under the **owner's own launchd jobs** (`com.fidelio.audiod`, `.cspot`,
  `.raop`, `.keepalive`). We never deploy them (B2 is on hold, §5).

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
  commits around them.
  - No source of a binary that is installed on a device changed since `5dc865f`.
  - Since `c46beee` every binary builds to a new sha256, because the toolchain puts a notice into it
    (§2.3). Those builds are on the iPad since 2026-11-22 and not yet on the phone (§2.4).
  - `55d1a82`, `926ea54`, `831ac15` and `b5e3106` (2026-11-22): the build no longer needs Apple's SDK
    (§6.3). No binary changed.
  - `b2a8ee3` (2026-11-22): the docs name where the toolchain is published. The tag **`toolchain-r3`**
    points at it, and the release of that name holds the toolchain tarball (§6.1).
- **B1.16.12** (2026-11-22, §3.1) is on the branch `b11612-dock-volume`, built and host-tested, not yet
  on `main` and with no marker: it waits for the device test. Only `fidelio-audiod` changed (§2.3).
- The owner said "You do the git updates": each round, fast-forward `main` and add a marker branch (§7).
- **Marker branches**, one per round:
  - Earliest rounds: `audiod-b11`, `-b12`, `-b13`, `-b15` … `-b19`, `-b110` … `-b116`. There is no `-b14`.
  - `audiod-b1161` (32af243), `-b116dr` (272c7e5), `-b116lp` (0b90521).
  - The second cloud session's: `-b1164` (ab0c7aa), `-b118`, `-b1165`, `-b1166`, `-b1167`, `-b1168`,
    `-b1169`, `-b11610`, `-b11611`.
  - voltest (5dc865f, b8d8976) and the tools and toolchain commits (6721da9, 5a2283c, c46beee, and the
    four sysroot commits 55d1a82, 926ea54, 831ac15, b5e3106) have no marker. Check with `git ls-remote --heads origin`.
- The old cloud session branch `ccr-afef93fb-x0jdve` is history, at `5dc865f`.
- **Patches:** the series `diffs/fidelio-0001…0049`, one per commit, existed only in the retired private
  store. The commits are the record.
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
| B1.16.8 | 4ed6a5a | 0045 | DockRemote hooks GraphicsServices `GSSendEvent` (observe-first) | the revision 3 build is in place on the phone since 2026-11-22 and loaded into iapd (load line seen); the observe run is open (§3.2) |
| B1.16.9 | 8eec470 | 0046 | NowPlaying 1.4.1: the iPad dock remote as a staged media-controller prototype | installed on both devices 2026-11-22 (revision 3 build); not exercised on the iPad |
| B1.16.10 | 4158342 | 0047 | `wake_mode` launch\|screen (the lock plugin as backstop) | its audiod is superseded by B1.16.11's |
| B1.16.11 | 7330752 | 0048 | lock plugin follows a live setting change and a SpringBoard restart | installed on both devices 2026-11-22 (revision 3 build); not exercised on the iPad |
| voltest | 5dc865f | 0049 | the volume probe (no marker) | ran on the phone once (paused) |
| tools | 6721da9 | none | cspot build date pinned (§6.3), host compiler check (§6.4); no binary changed | nothing to install |
| voltest | b8d8976 | none | the probe also shows iapd's side of the volume and can call `IAPAudioSetVolume` (§3.1) | on the phone since 2026-11-22; first readings in §2.5 and §3.1 |
| toolchain r2 | 5a2283c | none | toolchain revision 2: start-up objects built from Apple's open-source Csu, licence texts, `toolchain/pack.sh`, the toolchain's own sources in the repo under MIT, link-map and start-up checks in `build-arm.sh`; every binary keeps its sha256 | nothing to install |
| toolchain r3 | c46beee | none | toolchain revision 3: the start-up objects carry a notice that says where their source is (Apple's open-source licence asks for it "in the code itself"), so every binary carries it in a section `__TEXT,__csu_notice`; `build-arm.sh` requires revision 3 and checks the notice; every binary has the same size and a new sha256 (§2.3) | 2026-11-22: smoke test PASS and the suite installed on both devices (§2.4, §3.2) |
| sysroot | 55d1a82, 926ea54, 831ac15, b5e3106 | none | the build without Apple's SDK: `toolchain/sysroot/` makes a sysroot from a symbol list (stub libraries), Apple's published open-source headers (pinned) and framework headers of our own; `tools/build-arm.sh` uses it by default; every binary keeps its sha256 (§6.3) | nothing to install |
| B1.16.12 | branch `b11612-dock-volume` | none | audiod tells the dock a source's volume: after a set of the system volume in absolute mode it calls `IAPAudioSetVolume` (IAP.framework, dlopen'ed) with the same level, while an accessory takes the volume; never for an external change; a drag is coalesced (one call per 200 ms, the latest level last); the `hwvolume` line ends `; dock=<result>`; host suite `dockvol` (§3.1, §10.4) | built; device test pending |

### 2.3 Current builds: `tools/build-arm.sh`, toolchain revision 3 (the binaries of c46beee; audiod of B1.16.12)

The sources of each binary are those of the round named in the last column. Since c46beee only the
toolchain changed, and B1.16.12 changed audiod.

| Binary | Size | sha256 (toolchain revision 3) | Round |
|---|---|---|---|
| fidelio-audiod | 481,680 | `7c04dc0b61e15bd308a7a5f5c9d2071fca66007dc394cea6aed97086ad2f1923` | B1.16.12, branch `b11612-dock-volume` (the dock forward); before it, B1.16.11 7330752: 464,960, `30eb78eb0f6edb919d8091a2793e7097e2b5bcaf68bb60a690c878b85b89afe1` |
| fidelio-lockctl | 163,024 | `704c799f8b15122d8921cbdd9609c0e8136a85d038cc1a9c019b2c929db1ed12` | B1.16.6, aeed5b1 |
| fidelio-raop | 942,112 | `baf304f686d4bc63b8737942e935ba812746eb5b5451efe7c725daef3f07d132` | B1.16, a5ba534 (old id bf61abb; raop unchanged since) |
| cspot-ios3 | 2,290,160 | `35de013e2141c6a26cf8fe6a429f2f75cf99f9a75576c38720a1e1918e4b74fb` | B1.18, 47d4760 |
| NowPlaying.app/NowPlaying | 109,856 | `35bfa2294eca5fe00cb9d5e08a0056fc0b839f6831ce6b411191b4cd9e4ef256` | B1.16.9, 8eec470 (1.4.1) |
| Fidelio.lockbundle/Fidelio = Fidelio.bundle/Fidelio | 56,304 | `e708fab07b256ab3ae39f19eff40d1ddfbf7f245bd42a04435ea2b2c728a2df6` | B1.16.7, e3e448e |
| DockRemote.dylib | 34,640 | `8427742ca0e70a20c03c7d09884c47467169cb44d9c3efa0fedbb4981483621b` | B1.16.8, 4ed6a5a |
| voltest | 51,200 | `c4d1c4246bfed1c829a7b21f1c7df804b60b7c61f0b6110b8eeb3bbe116e111d` | probe, b8d8976 |

- All eight reproduce from nothing, on any day, at the reference paths of §6.1. Two builds from nothing
  on 2026-11-22 gave the same nine files. cspot-ios3's and DockRemote.dylib's hashes depend on those
  paths (§6.3).
- After a build the same list is in `$BUILD_ROOT/arm-out/SHA256SUMS`.
- **B1.16.12's audiod is 16,720 bytes larger.** Its `__text` grew by about 3.7 KB and `__cstring` by
  about 0.8 KB, and `__TEXT` crossed a 16 KB segment boundary (368 KB to 384 KB). It imports no new
  symbol (the indirect symbol tables of the two builds list the same names).
- **Built without Apple's SDK since `b5e3106`** (§6.3): the same nine files, byte for byte, as the build
  against the SDK.
- **What differs from the revision 2 build of the same sources:** one more section in each binary,
  `__TEXT,__csu_notice` (388 bytes in an executable, 313 in a dylib or a bundle), which holds the notice
  of the start-up object (§6.3).
  - No file size changed: the notice fits into the padding of `__TEXT`.
  - `__text` and the stubs start at a lower address, so address-dependent bytes differ. Every other
    section has the size it had.
- **On both devices** these builds run since 2026-11-22 (§2.4): the iPad has all but DockRemote and
  voltest, the phone has all eight.

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
- **Phone** (installed by this session on 2026-11-22, on the owner's "Update the iPhone and iPad"): the
  whole suite as built with toolchain revision 3, the first table of §2.3.
  - audiod `30eb78eb` (7330752), lockctl `704c799f`, raop `baf304f6`, cspot `35de013e` (47d4760), voltest
    `c4d1c424` (b8d8976), NowPlaying 1.4.1 `35bfa229`, `Fidelio.bundle` `e708fab0`. Each sha256 was read
    back on the device. The start-up smoke test passed first (§3.2).
  - What was there before is kept beside each file: `<name>.pre-r3.bak` in `/var/mobile/fidelio/b1/`,
    `/Applications/NowPlaying.app.pre-r3`, `Fidelio.bundle.pre-r3`, `settings.conf.pre-r3`. Before: audiod
    `c23cd624` (aeed5b1), lockctl `45da7833`, raop `9edce5e5`, cspot `2979aedd`, NowPlaying 1.4.0, bundle
    `559763a5`.
  - Settings now: `dock_remote=on`, `wake_on_play=on` (was off), `wake_mode=launch` (the owner's choice),
    `lock_plugin=on`, `keep_screen_on=on`; `lockplugin.debug` present.
  - **DockRemote 4ed6a5a** (`8427742c`, the revision 3 build) is in
    `/Library/MobileSubstrate/DynamicLibraries/` and loaded into iapd since an undocked iapd restart
    that day; the load line reads `DockRemote B1.16.8: hooked GSSendEvent with MSHookFunction from
    /usr/lib/libsubstrate.dylib`. `remote-tweak.observe` is set: it logs and passes every key through.
    The build before it is kept as `DockRemote.dylib.pre-r3.bak` (b051bfc).
  - **iapd logs its packets** since the same restart: `PrintIapPackets`, `LogDeviceVolumeChanges` and
    `LogLineOut` are set in `/var/mobile/Library/Preferences/com.apple.iapd.plist` (the file from before
    is `/var/mobile/fidelio/com.apple.iapd.plist.pre-r3`). The lines go to `/var/log/syslog`, which grows
    without rotation: every attach logs the dock's certificate. Remove the three keys when the volume
    work is done (undocked iapd restart).
  - After the install: the three jobs run, audiod got its AudioSession at the first attempt on the dock
    route (`USB`), Spotify played through it, and the wake brought NowPlaying up.
  - **The phone rebooted once that day**, about 20 s after a SpringBoard restart during playback
    (§2.5, §3.2). It came back by itself with everything running.
- **iPad** (installed by this session on 2026-11-22, on the owner's "Deploy to the dock"): the whole
  suite as built with toolchain revision 3, the first table of §2.3.
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

### 2.5 Device-proven facts (keep; don't re-test)
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
- **Dock keys as GraphicsServices events** (phone, DockRemote 4ed6a5a observing, 2026-11-22; four
  presses, compared with iapd's own log of the same presses):
  - The tweak's assumed layout is wrong. In the type-4001 record the word at offset 56 is not a 0/1
    state: it read 22 for menu (iapd key 22) and 0 for play/pause (iapd key 0), on both events of a
    press, and 21 on the single event logged for previous (iapd key 4). The half-word at offset 52 was
    0 every time.
  - Each press gave two type-4001 events that differ in the flags word: `0x400` first, `0x800` second
    (previous: only the `0x800` one). A type-101 event comes before each press and before each volume
    step.
  - So the swallow of 4ed6a5a cannot work as built (§3.3). A clean pass over all seven keys is still
    needed to map them.
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

### 3.1 B1.16.12: the phone's volume must move the dock (fix built, device test pending)
- **Report:** phone docked, `spotify_volume=absolute`. The Spotify slider moves NowPlaying's display and
  audiod logs `hwvolume: 'spotify' -> X (set X by 'spotify' slider Y)`, but the loudness doesn't change.
  The dock buttons work and show up as `hwvolume: 'external'`.
- **State on 2026-11-22, evening: the fix is built; the device test is next.**
  - The cause (§2.5): nothing on the phone forwards the system volume to iapd, so the dock never hears
    of a change made on the phone. The owner confirmed by ear on 2026-11-22 that the dock follows
    `IAPAudioSetVolume`: quieter, louder and back with `voltest iap set`.
  - The fix (branch `b11612-dock-volume`, audiod only; §10.4): in absolute mode, after a source's set
    of the system volume (the Spotify slider, the AirPlay volume), audiod calls `IAPAudioSetVolume` with
    the same level, when `IAPAudioIsVolumeControlSupported` says yes at that moment. Never for an
    external change (the dock's buttons, another app). A drag is coalesced: one call per 200 ms at
    most, the latest level last. The `hwvolume` line ends `; dock=ok` when the dock was told.
  - **The device test:** install audiod from the branch on the phone, dock it, play Spotify in
    absolute mode and move the slider. Pass: the loudness follows, the event lines end `; dock=ok`, and
    the start-up line reads `dock volume (B1.16.12): available (IAP.framework: ...)`. Also: the dock's
    buttons still work and give `hwvolume: 'external'` lines with no `dock=`; a drag gives `dock=deferred`
    lines followed by one `hwvolume: dock told X, the latest of N held sets: dock=ok`; on the iPad the
    start-up line shows whether its IAP has the two functions, and nothing else changes there unless an
    accessory that takes the volume is attached.
  - Then fast-forward `main` with a marker branch (§7).
  - The relabel of the settings below is still wanted and is not part of this round.

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
   show `lock-plugin re-enabled Fidelio after a SpringBoard restart`. Also flip "Lock-screen plugin" off
   and on while playing.
   - **Phone, 2026-11-22: not passed, and not safe to repeat as it is.** The respring during playback
     ended in a reboot of the phone about 20 s later (§2.5); the line was never logged. Find the cause
     first: the owner decides whether and how to reproduce it (one variable at a time: the tweak
     disabled with `remote-tweak.disabled`, the lock plugin off, nothing playing).
   - iPad: open. Ask the owner before doing it there, given the above.
3. **B1.16.8 DockRemote observe** (phone): installed and loaded on 2026-11-22, `remote-tweak.observe`
   set (§2.4).
   - First data, four presses (§2.5): the layout the tweak assumes is wrong, so "which state value is
     down" has no answer in that field.
   - Still needed: one clean pass, a few seconds apart, in this order: play/pause, next, previous, menu,
     select, up, down. Then read `remote-events.log` beside iapd's `SimpleRemoteEventHandler got
     evKeyDown ... key:N` lines in the syslog.
4. **B1.16.9** (iPad): NowPlaying 1.4.1, stages 1 → 2 → 3 via `remote-control.stage`.
   - Does `remote-control.log` fill from the dock keys, and with which subtypes?
   - Who gets the keys when the Music app was the last to play?
5. Still never reported: iPad rotation and cover sharpness (sharp covers need cspot 47d4760 as well).

### 3.3 After those results
- **DockRemote swallow:** the values differ (§2.5), and by more than `KEY_STATE_DOWN`: the key is in the
  word the tweak reads as the state, and down and up differ in the flags word. `DockRemoteHook.mm` needs
  a new decode, built from the clean seven-key pass of §3.2. Do not set `remote-tweak.swallow` on the
  4ed6a5a build.
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
- Old device reads, never reported: B1.13 AirPlay volume and `dmcp.device-volume`; B1.12 Spotify pausing
  on an AirPlay takeover; the B1.5 `--no-backpressure` A/B.

### 3.5 B1.17: the rename `fidelio` → `dock-receiver`, with packaging (HOLD until the owner says device testing is done)
- The plan is in [`docs/RENAME-PLAN.md`](RENAME-PLAN.md): the name map, the compatibility constraints, the
  migration and the deliverable, and since 2026-11-18 the packaging (§6 there).
- It is one round, the last in the queue. Re-run it mechanically on the final tree.
- Still on HOLD until the owner says device testing is done.

### 3.6 B2: lifted for packaging only
- The owner decided on 2026-11-18 that packaging (Debian packages with the launchd jobs and the install
  scripts) is built in the rename round (§3.5). Until that round: no deploy automation, no launchd plists.
- Hardware switch-test automation stays on HOLD until the owner says go.

### 3.7 After the rename: release CI
- GitHub CI that makes a release on every merge to `main`, with an automatic version bump (the owner,
  2026-11-18). Nothing built.
- It needs one version number for the whole suite, stamped into the app and the daemons at build time.
- The owner's direction (2026-11-21): a release includes the packages and the toolchain tarball.
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
2. **B1.17:** go, once the owner is satisfied with the device tests.
3. **Licence points found while preparing the release** (2026-11-21 and 11-22; facts gathered, not
   judged). Two are settled by the owner's decisions of 11-22 (§5.2); the rest are open.
   - **Settled: the notice for the start-up code.** Apple's open-source licence asks, for object code,
     for "a prominent notice, in the code itself as well as in related documentation" saying where the
     source is. Since toolchain revision 3 the notice is in the code (§6.3);
     `toolchain/THIRD-PARTY-NOTICES.md` §9 is the documentation notice. The packages need the
     documentation notice as well. Whether a text section counts as "prominent" has not been judged.
   - **Settled: the terms of `cspot-ios3`.** The tree we build the Spotify library from has no licence
     file, while its original upstream states GPL-3.0-or-later. The top-level `LICENSE` now says that
     this binary as a whole is distributed under GPL-3.0-or-later. What its package must then carry is
     in `docs/RENAME-PLAN.md` §6.
   - Open: `bell`, the library cspot is built on and which is linked into `cspot-ios3`, has no licence
     file, neither in the tree we build from nor in its upstream repository (checked on 2026-11-22).
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
   - Open: the notices for the third-party code that is linked into the device binaries (the Spotify
     library and what it bundles, mbedTLS, the C++ runtime) have not been drawn up. The packages need
     them.
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

The volume default is no longer a question: the owner decided on 2026-11-18 (§3.1, §5.2).

---

## 5. Standing rules and decisions

### 5.1 Rules (verbatim intent; never break)
1. **Never** put the Spotify client-id/secret or ANY credential in any file. "Scripts are ok, no secrets."
2. **Never write the devices' root password** in any file, commit, log excerpt or message.
3. **B2 on HOLD** until the owner says go.
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
10. **`main` moves by fast-forward only;** add a marker branch `audiod-b116NN` per device round.

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
    that says how to fill it.
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
- **Naming** (from before): the app is "NowPlaying"; new components get neutral names; the advertised device
  names stay. The full rename is B1.17 only.

### 5.3 Naming rule
Describe the software by function. Use "Fidelio" only as Philips' dock name, with the
not-affiliated-with-Philips line in the README. Do not rename existing binaries, directories, classes or
`/var/mobile/fidelio` before B1.17.

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
  | repo | `/home/user/fidelio` |
  | `BUILD_ROOT` (left unset) | `/home/user/build` |

- **The WSL clone** `/home/user/fidelio` is a git clone of the Windows clone, so its `origin` is
  `/mnt/c/code/fidelio/github/fidelio`. Fetch and fast-forward on the Windows side first, then `git pull`
  in WSL.
- **Build only inside the distro's own filesystem,** never under `/mnt/c`.
- **Builds and test runs go in subagents, one at a time** (the owner's wish; the scripts share one
  `BUILD_ROOT`). Since 2026-11-22 the iterative build, compare and fix work goes to subagents on
  smaller models, and the session plans, reviews the diffs and pushes (also the owner's wish).
- **Packages:**
  `apt-get install clang-18 llvm-18 cmake ninja-build protobuf-compiler python3-protobuf libavahi-client-dev g++ unifdef flex bison`.
  - `g++` gives the host suite its `cc` and `c++`.
  - `unifdef`, `flex` and `bison` are needed when `tools/build-arm.sh` makes the sysroot itself (§6.3).
    Ubuntu 24.04's flex 2.6.4 and bison 3.8.2 were tested.
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
    `/home/user/build.own-a` the first of the two built without it, and `/home/user/build` the second.
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
  - audiod and lockctl are signed in CMake POST_BUILD with `common/fidelio-springboard.entitlements.plist`
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
It builds the host targets (`$BUILD_ROOT/host/...`) and runs **23 suites one after another**. They share
`/tmp/audiod-*` dirs and ports: never run two at once, and **don't run CPU-heavy work alongside**, since
the suites are timing-sensitive.

| suite | checks | suite | checks | suite | checks |
|---|---|---|---|---|---|
| framing | 26 | silence | 22 | lockplugin | 8 |
| aqsink | 28 | crossfade | 5 | preempt | 11 |
| aqsink_bare | 23 | producers | 21 | ap_preempt | 11 |
| writeframe | 2 | pacing | 8 | ap_volume | 11 |
| settings | 23 | wake | 11 | volume | 11 |
| settings_dmn | 29 | unlock | 23 | layout | 30 |
| control | 8 | arbiter | 13 | lock_layout | 10 |
| cover_url | 6 | dockvol | 14 | | |

- **Baseline: 354 ok, 0 FAIL** (340 before B1.16.12 added `dockvol`).
- **It needs `cc` and `c++`** (apt `g++`): the plain tests call them by name, and they are CMake's default
  compilers. Since `6721da9` the script checks for both.
- **Exit codes:** 0 all passed first time; 1 a persistent failure; **2 passed only after a re-run**
  (`producers` has a known timing flake in its HWVOLUME check, so report exit 2 honestly); 3 setup error.
- **Environment:**
  - `RUNS` (default 3); `SUITES="producers pacing"` runs only those; `SKIP_BUILD=1`.
  - `SUITE_TIMEOUT` (900 s); `PYTHON`; `STRICT_COUNTS=1` (an ok count off the baseline fails the suite).
  - `HOST_CC` / `HOST_CXX` apply to a fresh configure only.
- **Logs:** `$BUILD_ROOT/host-test-logs/<timestamp>/` (`latest` links to it), with `summary.txt`. A failed
  run's `/tmp/audiod-*` dirs are copied there.
- About **9.5 minutes** (575 s on the owner's PC): run it in the background, alone, and wait for it to
  finish.
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
  `AUDIOD_STALL_ACTIVE_MS`.
- **Mock dock** (B1.16.12, `audiod/src/HardwareVolume.*`): `AUDIOD_MOCK_IAP=ok|unsupported|fail|query-fail`
  (unset = IAP unavailable); each call it "makes" is logged as `mock iap: IAPAudioSetVolume(L) -> R`.
  Test hook: `AUDIOD_DOCK_FORWARD_MIN_MS` (the coalescing window, default 200 ms).
- **Practice:**
  - A new test must be shown to **fail against the previous binary**. Build the old commit in a temporary
    `git worktree`, as B1.16.6 did: 6 FAILs against b051bfc. B1.16.12's `dockvol`: 11 FAILs against
    1a6ce9e (the other 3 are the "never told" checks, which the old binary passes by doing nothing).
  - Plain-C layout cores have host tests that pin the device-proven frames: `nowplaying/src/NPLayout.c` with
    `layout_test.c`, and `nowplaying/lockplugin/LPLayout.c` with `lock_layout_test.c`.

### 6.6 Per-component notes
- **audiod:** CMake (`audiod/CMakeLists.txt`), host and ARM, via the scripts.
  - `fidelio-audiod --help` lists every flag.
  - The registry (§10.2) is bound to the real options: `read()`/`write()`, `selfCheck()` logs drift.
- **NowPlaying:** `nowplaying/src/build.sh` (the file list is inside). `gen_images.py` makes the icons,
  tab glyphs, placeholders and launch images, and the build fails if one is missing.
  - The version is in `FidelioStatus.h` and `Info.plist` (1.4.1).
  - After installing on a device, run `su mobile -c uicache` (device family, icons, URL scheme).
- **Lock plugin:** `nowplaying/lockplugin/build.sh OUTDIR lockbundle|bundle`. The binary and Info.plist are
  the same for both.
- **DockRemote:** `tweak/DockRemote/build.sh` (C++17/libc++ static, ObjC++).
- **voltest:** `audiod/probe/build.sh`.

---

## 7. Round checklist

1. **Verify** the reported problem in the code (§5.1.4). Write the fix plus host tests; show the new test
   fails against the old binary when practical.
2. **ARM build** (`tools/build-arm.sh`): check warnings (`grep -i warning` in `$BUILD_ROOT/logs/arm-*.log`;
   only the toolchain's `-mllvm -fast-isel=0` notes are expected).
3. **Full host suite** in the background (§6.4).
4. **Commit.** Message: a subject `B1.16.N: …`, a body saying what was verified and why, then the trailers
   (§5.1.9).
   - **Never claim test results that haven't finished.** If the commit comes before the suite ends, word
     the message accordingly.
5. **Rebuild ARM from the clean commit** (pull it into the WSL clone first, §6.1), then fast-forward `main`
   and add the marker in one push from the Windows clone, whose `origin` is GitHub:
   ```sh
   git fetch -q origin main && git merge-base --is-ancestor origin/main <sha> && git push origin <sha>:refs/heads/main <sha>:refs/heads/audiod-b116NN
   ```
   A tools-only round gets no marker (voltest and `6721da9` have none).
6. **`docs/HANDOFF.md`:** make sure it is current. §5.1.6 puts the update in the round's own commit: the
   state (§2), the rounds table, the hashes, the open work (§3).
7. **Install** from `/home/user/build/arm-out`, on the owner's go-ahead (§8).
   - The app: `su mobile -c uicache` afterwards.
   - The lock bundles need a respring.
   - DockRemote is installed undocked, then `killall iapd`.
8. **Tell the owner the outcome in plain words.**

---

## 8. Device work
- The session has SSH to both devices, as root, from the PC.
- It runs device tests **only on the owner's go-ahead**. Hands-on steps wait until the owner is at the
  dock.
- **Logs only, trimmed. Never credentials** (§5.1.1, §5.1.2).
- **cycript only standalone,** never `-p SpringBoard` (§2.5).
- **Never restart iapd while the phone is docked.** It half-negotiates the accessory, and only reseating
  the phone fixes it.
- **Only one audiod may run at a time.**
- **A device observation is evidence, not truth:** verify it against the code (§5.1.4).
- **Probes and traces available:** `voltest`, the plugin trace (`lockplugin.debug` → `lockplugin.log`),
  `remote-events.log`, `remote-control.log`, and audiod's stdout and event log.
- Device probes found the 4th entitlement, the false negative, the dim behaviour, iapd's imports and the
  route with no volume control.

---

## 9. Files on the devices (`/var/mobile/fidelio/`, all user `mobile`-readable)
- **Written by the daemons:**
  - `audiod.sock`; `active-source` (rewritten every 10 s; "none" when idle).
  - `status-spotify.plist` / `status-airplay.plist` (and `.json`); `cover-spotify.jpg` / `cover-airplay.jpg`.
  - `settings-<daemon>.manifest.plist`.
- **`settings.conf`:** `<daemon>.<key>=<value>` lines, written by the Settings tab, live-reloaded about
  once a second for live options (others need a restart).
- **`lockplugin.plist`:** `KeepScreenOn`, written by audiod (the single owner).
- **Lock plugin trace:** `lockplugin.debug` (the flag), `lockplugin.log`.
- **DockRemote:** `remote-tweak.observe`, `remote-tweak.swallow`, `remote-tweak.disabled` (the kill
  switch), and `remote-events.log` (the load line on every iapd start, plus the observe lines).
- **NowPlaying 1.4.1:** `remote-control.stage` (1–3), `remote-control.log`; and the old probe
  `remote-probe.on`, which logs to `remote-events.log` from the app.
- **Installed elsewhere:**
  - The lock bundles: `/System/Library/SpringBoardPlugins/`. Respring afterwards.
  - DockRemote: `/Library/MobileSubstrate/DynamicLibraries/DockRemote.{dylib,plist}` (filter: iapd).
    Install it undocked, then `killall iapd`.
  - The app: `/Applications/NowPlaying.app`.

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
    `SBSLaunchApplicationWithIdentifier(com.fidelio.nowplaying)`.
    - On 3, with `unlock_on_play` on: `SBSOpenSensitiveURLAndUnlock(nowplaying:, true)` once.
    - true → `wake: device locked, unlock accepted (nowplaying:)`, and the plugin stays off for that start.
    - false → `unlock request failed`, and the plugin goes on.
  - **`wake_mode=screen`:** undim and reset the idle timer only, with the plugin enabled first:
    `wake (screen only)`.
- **The plugin (`lock_plugin`):** `SBEnableLockScreenBundle(port, 1/0, "Fidelio")`.
  - Disabled when nothing is active.
  - **B1.16.11 `syncLockPlugin`:** applies a live setting change, keeping the unlock rule; it disables
    under the enabled name. While enabled, it polls SpringBoard's pid every 2 s (sysctl `KERN_PROC_ALL`)
    and re-enables 4 s after a new pid.
- **The plugin itself** (`FidelioLockPluginController`):
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
    out from the run loop when the window ends. An external change drops a held level.
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
- **Phone, B1.16.8:**
  - `MSHookFunction(GSSendEvent)`, resolved where it is exported.
  - A key is a **type 4001** `GSEventRecord`: header 52 bytes (type @0, subtype @4, flags @40, infoSize @48);
    info is `{u16 key @52, int state @56}`.
  - It is decoded only when type is 4001 and infoSize is 8.
  - The observe log line: `gs type= subtype= flags= infoSize= port= [KEY key= state=] hdr=[…] info=[…]`.
  - Swallow forwards 0/3/4 on state 1 and drops down and up, only when `dock_remote=on`, a Fidelio source is
    active and audiod is reachable (`DockRemoteCore.cpp`).
  - Proven by disassembling the 3.1.2 SDK's GraphicsServices (armv6): `GSEventAccessoryKeyStateChanged`
    builds that record and calls `GSSendSystemEvent`, which is `GSSendEvent(rec, the purple system port)`.
    iapd builds the record itself.
- **iPad, B1.16.9:**
  - NowPlaying's `NPRemoteControl.m`: the window class `NPWindow` can become first responder.
  - `NPApplication` (the principal class, `NPEventProbe.m`) catches remote-control events (type 2, subtypes
    100–109) in `sendEvent:` and at the end of the responder chain, and handles each once.
  - Stage 2 dlsyms `MRMediaRemoteSetCanBeNowPlayingApplication`. Stage 3 plays silence in a **mixable**
    session through `AVAudioPlayer`, because a non-mixable one would interrupt audiod.

### 10.6 NowPlaying
- **Structure:**
  - `FidelioAppDelegate` sets `setIdleTimerDisabled:YES` at launch, so an unlocked device with the app in
    front never auto-locks.
  - The 1 Hz tick refreshes `FidelioStatus`.
  - The cspot watchdog runs `killall -9` on a stale status.
- **Layout:** `NPLayout.c`; the phone is pinned to the B1.16.1 frames, and the tablet layout is size-driven
  with rotation.
- **Settings tab:** built from the manifests, with info (ⓘ) alerts. Changing a non-live option restarts the
  daemon.

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
- **Installing over SSH:** never print the launcher scripts, `creds.env` or a process list with
  arguments: they hold the Spotify credentials. Check a process with `killall -0 NAME`.
- **Phone:** no cycript, so ship a probe binary.
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
On the branch `b11612-dock-volume`, not yet on `main`: `B1.16.12: tell the dock a source's volume
(IAPAudioSetVolume)`, on top of 1a6ce9e.
```
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
