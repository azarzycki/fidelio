# HANDOFF — the dock-receiver stack (`azarzycki/fidelio`), state as of 2026-11-18

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
2. **The build environment** is a WSL2 distro on the same PC (§6.1): toolchain, SDK, a second clone and
   `BUILD_ROOT` at the reference paths.
   - Bring the WSL clone up to date: fetch and fast-forward on the Windows side first, then `git pull` in
     WSL.
   - Builds and test runs go in subagents, one at a time (§6.1).
3. **Prove the baseline:**
   - `tools/build-arm.sh` → `result: OK` with the hashes in §2.3.
   - `tools/run-host-tests.sh` → `TOTAL host: 340 ok, 0 FAIL`, 22 suites, about 9.5 minutes. Run it alone
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

**Components** (one monorepo, everything built ARMv6 against the iPhoneOS 3.1.2 SDK):
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
- `main`: see `git log`. On 2026-11-18 it holds `6721da9` (tools only), `dfbbc3d` (this record moved
  into the repo), `b8d8976` (the volume probe extended), `5a2283c` (toolchain revision 2, §6.1) and the
  docs commits around them. No binary that is installed on a device changed since `5dc865f`.
- The owner said "You do the git updates": each round, fast-forward `main` and add a marker branch (§7).
- **Marker branches**, one per round:
  - Earliest rounds: `audiod-b11`, `-b12`, `-b13`, `-b15` … `-b19`, `-b110` … `-b116`. There is no `-b14`.
  - `audiod-b1161` (32af243), `-b116dr` (272c7e5), `-b116lp` (0b90521).
  - The second cloud session's: `-b1164` (ab0c7aa), `-b118`, `-b1165`, `-b1166`, `-b1167`, `-b1168`,
    `-b1169`, `-b11610`, `-b11611`.
  - voltest (5dc865f, b8d8976) and 6721da9 have no marker. Check with `git ls-remote --heads origin`.
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
| B1.16.8 | 4ed6a5a | 0045 | DockRemote hooks GraphicsServices `GSSendEvent` (observe-first) | staged on the PC, not installed |
| B1.16.9 | 8eec470 | 0046 | NowPlaying 1.4.1: the iPad dock remote as a staged media-controller prototype | not installed |
| B1.16.10 | 4158342 | 0047 | `wake_mode` launch\|screen (the lock plugin as backstop) | its audiod is superseded by B1.16.11's |
| B1.16.11 | 7330752 | 0048 | lock plugin follows a live setting change and a SpringBoard restart | not installed |
| voltest | 5dc865f | 0049 | the volume probe (no marker) | ran on the phone once (paused) |
| tools | 6721da9 | none | cspot build date pinned (§6.3), host compiler check (§6.4); no binary changed | nothing to install |
| voltest | b8d8976 | none | the probe also shows iapd's side of the volume and can call `IAPAudioSetVolume` (§3.1) | not run on a device yet |
| toolchain r2 | 5a2283c | none | toolchain revision 2: start-up objects built from Apple's open-source Csu, licence texts, `toolchain/pack.sh`, the toolchain's own sources in the repo under MIT, link-map and start-up checks in `build-arm.sh`; every binary keeps its sha256 | nothing to install |

### 2.3 Current builds: `tools/build-arm.sh` at 5a2283c, toolchain revision 2 (the hashes of b8d8976)

| Binary | Size | sha256 | Round |
|---|---|---|---|
| fidelio-audiod | 464,960 | `dd2bc78706255db1114b1b21a1ec7f214d8177056c1eb4b370e5916ba1691e42` | B1.16.11, 7330752 (includes wake_mode, 4 keys) |
| fidelio-lockctl | 163,024 | `45da7833232a779f9dbf2969b19c7a1e8bac9d612b1c43a75f00c0ea5412a212` | B1.16.6, aeed5b1 |
| fidelio-raop | 942,112 | `9edce5e531c02534244817d5050152851bd0d08cc4d49321a0b74800d103b89a` | B1.16, a5ba534 (old id bf61abb; raop unchanged since) |
| cspot-ios3 | 2,290,160 | `2979aeddf6cebb5545bdbd9f5dd37b2eaf6d6f66cd8f623ab91cd64f76671f82` | B1.18, 47d4760 |
| NowPlaying.app/NowPlaying | 109,856 | `f23daec8999e7314fac17920e5c7b62856c7654c345266670836bf9b5504f273` | B1.16.9, 8eec470 (1.4.1) |
| Fidelio.lockbundle/Fidelio = Fidelio.bundle/Fidelio | 56,304 | `559763a5b019d4f478eb86477bb37b02e3befbacffc86878b284d8fd7f927ae4` | B1.16.7, e3e448e |
| DockRemote.dylib | 34,640 | `4ae947d5f0d2b55e04b6d780eeafab61741180eef0987052725b692bfa8ca639` | B1.16.8, 4ed6a5a |
| voltest | 51,200 | `8c9f1f0372f05643f34d088fc4d6b16538dbf5eefd2fb8354d16e851754299f1` | probe, b8d8976 (the 5dc865f one was 51,056, `bb956992…f956b9`) |

- Each is byte-identical to the build the cloud session published for its round.
- All eight reproduce from nothing, on any day, at the reference paths of §6.1. cspot-ios3's and
  DockRemote.dylib's hashes depend on those paths (§6.3).
- After a build the same list is in `$BUILD_ROOT/arm-out/SHA256SUMS`.

### 2.4 What runs on each device (the last device reports)
- **Phone:**
  - audiod `c23cd624` (aeed5b1, B1.16.6) and lockctl `45da7833`.
  - `Fidelio.bundle` `559763a5` (B1.16.7).
  - NowPlaying 1.4.0 (B1.18).
  - DockRemote **b051bfc** (inert: no flags), loaded into iapd.
  - raop/cspot: B1.16-era builds. Whether cspot 47d4760 was installed is not reported.
  - Settings, as left for the lock-plugin test: `wake_on_play=off`, `lock_plugin=on`, `keep_screen_on=on`,
    `lockplugin.debug` present.
- **iPad:**
  - audiod `c23cd624` (aeed5b1) and `Fidelio.lockbundle` `559763a5` (B1.16.7).
  - NowPlaying 1.4.0.
  - `lock_plugin` and `keep_screen_on` on, set explicitly. Both **default off**.
- **Not yet installed anywhere:** audiod 7330752 (B1.16.10/11), DockRemote 4ed6a5a (B1.16.8),
  NowPlaying 1.4.1 (B1.16.9).

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

---

## 3. Open work, in order

### 3.1 B1.16.12: the phone's volume must move the dock (investigation in progress)
- **Report:** phone docked, `spotify_volume=absolute`. The Spotify slider moves NowPlaying's display and
  audiod logs `hwvolume: 'spotify' -> X (set X by 'spotify' slider Y)`, but the loudness doesn't change.
  The dock buttons work and show up as `hwvolume: 'external'`.
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
1. Install audiod **7330752** on both devices. Set `wake_on_play=on`, and `wake_mode=launch` on the phone (the
   owner's choice).
2. **B1.16.11 check:** with the plugin on and music playing, respring. Within ~6 s the event log should
   show `lock-plugin re-enabled Fidelio after a SpringBoard restart`. Also flip "Lock-screen plugin" off
   and on while playing.
3. **B1.16.8 DockRemote observe** (phone): install 4ed6a5a undocked, `killall iapd`, and check the load line
   in `remote-events.log`.
   - Then dock, `touch remote-tweak.observe`, and press all 7 keys.
   - Read the `gs type=4001 … KEY key= state=` lines. Which state value is "down"? The swallow assumes 1.
4. **B1.16.9** (iPad): NowPlaying 1.4.1, stages 1 → 2 → 3 via `remote-control.stage`.
   - Does `remote-control.log` fill from the dock keys, and with which subtypes?
   - Who gets the keys when the Music app was the last to play?
5. Still never reported: iPad rotation and cover sharpness (sharp covers need cspot 47d4760 as well).

### 3.3 After those results
- **DockRemote swallow:** if the state values match (down = 1), no rebuild is needed.
  `touch /var/mobile/fidelio/remote-tweak.swallow` and set `audiod.dock_remote=on`.
  If the values differ, change `KEY_STATE_DOWN` in `DockRemoteHook.mm` and rebuild.
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
  Nothing has been published yet.
- How CI gets Apple's SDK is open (§4).

---

## 4. Questions waiting for the owner
1. **Bonjour keep-warm** (§3.4): build it or not.
2. **B1.17:** go, once the owner is satisfied with the device tests.
3. **Licence points found while preparing the release** (2026-11-21; facts gathered, not judged):
   - Apple's open-source licence asks, for object code, for a notice "in the code itself as well as in
     related documentation" saying where the source is. The start-up objects carry no notice text;
     `toolchain/THIRD-PARTY-NOTICES.md` §9 is the documentation notice. The packages will need the same
     notice.
   - Four toolchain components ask for source to be offered (the linker tools, a disassembler inside
     `otool`, ldid, libplist); the notices file points to their sources and our patches. ldid is AGPL
     with a statically linked Apache-licensed libcrypto: upstream's combination, not assessed.
   - The Spotify library: the tree we build from has no licence file, while its original upstream
     states GPL-3.0-or-later. This bears on the terms of the `cspot-ios3` package.
4. **How release CI gets Apple's SDK** (§3.7). The owner wants no exposure to Apple (§5.2). The repo is
   public, and so are its Actions logs. Proposed, not decided:
   - the host suites on GitHub's runners (they need no SDK);
   - the ARM build and the release on a runner on the owner's PC, so the SDK never leaves it.

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
  | SDK | `/home/user/sdk/iPhoneOS3.1.2.sdk` |
  | repo | `/home/user/fidelio` |
  | `BUILD_ROOT` (left unset) | `/home/user/build` |

- **The WSL clone** `/home/user/fidelio` is a git clone of the Windows clone, so its `origin` is
  `/mnt/c/code/fidelio/github/fidelio`. Fetch and fast-forward on the Windows side first, then `git pull`
  in WSL.
- **Build only inside the distro's own filesystem,** never under `/mnt/c`.
- **Builds and test runs go in subagents, one at a time** (the owner's wish; the scripts share one
  `BUILD_ROOT`).
- **Packages:**
  `apt-get install clang-18 llvm-18 cmake ninja-build protobuf-compiler python3-protobuf libavahi-client-dev g++`.
  - `g++` gives the host suite its `cc` and `c++`.
  - `tools/_common.sh` prints exactly this list if something is missing.
  - The host clang is Ubuntu 24.04's `clang-18` (18.1.3).
- **The toolchain's own files are in the repo** (`toolchain/`, since `5a2283c`); its tarball and the SDK
  are not.
  - **Toolchain tarball, revision 2:** `armv6-ios3-toolchain-linux-x86_64.tar.xz`, 3,976,540 bytes, sha256
    `1f1cd6300985b4027b178024e1dea1740b4f2db9dfcf206f6bcc31430f46b078`. On the PC it is
    `C:\code\fidelio\cloud-build\armv6-ios3-toolchain-linux-x86_64-r2.tar.xz`; it is the one installed in
    WSL. `toolchain/pack.sh --base <an unpacked toolchain>` rebuilds it, byte for byte, from the repo and
    the upstream-built parts of a base toolchain (`toolchain/README.md`, `spotify/BUILD.md` §6).
    - Against revision 1 it adds the start-up objects (`armv6-ios3/lib/startfiles/`, built from Apple's
      open-source Csu, tag Csu-79), `LICENSES/`, `THIRD-PARTY-NOTICES.md`, `LICENSE` and `VERSION`, and
      carries the current `BUILD.md`. The host tools, the libc++ headers and the four ARMv6 archives
      are revision 1's, byte for byte.
    - Its wrapper puts the toolchain's start-up objects ahead of the SDK's on every link. Linked
      outputs are byte-identical to revision 1's.
    - It contains no file of the SDK (checked by hash against all 2,228 SDK files).
  - **Revision 1** (3,923,700 bytes, sha256 `c07f80c4…dbff`) is kept beside it and as
    `/home/user/opt/armv6-ios3-toolchain.r1`. Do not publish it: it has no licence texts and an old copy
    of the build notes. `tools/build-arm.sh` refuses it.
  - **SDK tarball** `iPhoneOS3.1.2.sdk.tar.xz`: 97,995,312 bytes, sha256
    `d5122c3bd2ddd217de918ff3635c590261424dc7f3a5e63d4cbd8417bec2d5e2` (verified identical to Apple's DMG).
    It is kept at `C:\code\fidelio\`. It is Apple's: never commit it, and never publish it or a link to a
    copy (§5.2). `spotify/BUILD.md` §1 says how it was checked.
  - Unpacking, if the distro has to be set up again:
    ```sh
    mkdir -p /home/user/opt && tar xf armv6-ios3-toolchain-linux-x86_64.tar.xz -C /home/user/opt
    # extract the SDK so that /home/user/sdk/iPhoneOS3.1.2.sdk/usr/lib/libSystem.B.dylib exists
    export IOS3_SDK=/home/user/sdk/iPhoneOS3.1.2.sdk
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
`IOS3_SDK` is required; the toolchain comes from `PATH` or `IOS3_TOOLCHAIN`.
- **It builds:** mbedTLS; audiod and lockctl (CMake); raop; cspot (deps); NowPlaying, both lock bundles,
  DockRemote and voltest (their `build.sh`).
- It collects everything into `$BUILD_ROOT/arm-out`, then verifies each binary: size, sha256,
  `armv6-ios3-check`, the post-v6 count, and the entitlement keys against the plist.
- **Since `5a2283c` it needs toolchain revision 2** and also checks, for every binary:
  - its link map (`$BUILD_ROOT/arm/maps/<step>/`): no `.o` file or archive member under `$IOS3_SDK` was
    linked, and exactly one start-up object, the toolchain's;
  - `tools/startcheck.py`: the start-up code inside the binary. It exists because start-up objects
    built with the Csu Makefile's `ld -r` step are wrong and still pass `armv6-ios3-check` and
    `isa_scan.py`;
  - the toolchain's three start-up objects against the sha256 pins in `tools/_common.sh`.

  All three were shown to fail on deliberately bad set-ups (the commit message of `5a2283c`).
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
It builds the host targets (`$BUILD_ROOT/host/...`) and runs **22 suites one after another**. They share
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
| cover_url | 6 | | | | |

- **Baseline: 340 ok, 0 FAIL.**
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
- **Practice:**
  - A new test must be shown to **fail against the previous binary**. Build the old commit in a temporary
    `git worktree`, as B1.16.6 did: 6 FAILs against b051bfc.
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
- **iPad:** no syslog (file traces only), and the shell tools are missing (§1).
- **Phone:** no cycript, so ship a probe binary.
- **WSL** (§6.1):
  - Multi-line commands passed through PowerShell lose quotes and backslashes. Pipe a single-quoted heredoc
    into `wsl -d Ubuntu2404 -u root -- bash -s`.
  - Never `nohup` or `&` inside WSL: the distro is torn down when the calling `wsl.exe` exits. Background
    the tool call instead.
  - WSL appends the Windows `PATH`: set a clean one.
  - Build only inside the distro's own filesystem, never under `/mnt/c`.
  - One build or test run at a time: the scripts share one `BUILD_ROOT`.

---

## 12. Appendix

### 12.1 `main`, newest first (subjects shortened)
```
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
